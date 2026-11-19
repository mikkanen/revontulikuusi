#include "audio_capture.hpp"

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <random>
#include <thread>
#include <vector>

namespace {

void fft(std::vector<std::complex<float>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * std::numbers::pi_v<float> / static_cast<float>(len);
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (std::size_t j = 0; j < len / 2; ++j) {
                auto u = a[i + j];
                auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

enum class BandType { Bass, Mid, Treble };

struct Led {
    sf::Vector2f pos;
    sf::Color base_color;
    BandType band;
    float phase;
    float speed;
    float radius;
};

struct Snowflake {
    sf::Vector2f pos;
    float vy;
    float radius;
    float drift_speed;
    float phase;
    std::uint8_t alpha;
};

struct BgStar {
    sf::Vector2f pos;
    float radius;
    float phase;
};

struct ShootingStar {
    bool active{false};
    sf::Vector2f pos{};
    sf::Vector2f vel{};
    float age{0.f};
    float max_age{1.1f};
    float tail_length{150.f};
};

struct GiftBox {
    float x, ground_y, w, h;
    sf::Color box_color, ribbon_color;
};

struct SmokePuff {
    sf::Vector2f pos;
    float age;
    float max_age;
    float start_radius;
    float drift_offset;
};

struct BgTree {
    float x;
    float height;
    float half_width;
    int tiers;
    bool far_layer;
};

sf::ConvexShape create_star(float outer_r, float inner_r) {
    sf::ConvexShape star(10);
    for (int i = 0; i < 10; ++i) {
        float angle = -std::numbers::pi_v<float> / 2.0f +
                      i * (std::numbers::pi_v<float> / 5.0f);
        float r = (i % 2 == 0) ? outer_r : inner_r;
        star.setPoint(i, {std::cos(angle) * r, std::sin(angle) * r});
    }
    return star;
}

} // namespace

void process_audio_block(const float* samples, std::size_t count, AudioData& audio) {
    if (!samples || count < 16) return;

    // Varmistetaan kahden potenssi FFT:lle (oletus 1024)
    std::size_t fft_size = 1;
    while ((fft_size << 1) <= count) {
        fft_size <<= 1;
    }

    static std::vector<float> hann_window;
    if (hann_window.size() != fft_size) {
        hann_window.resize(fft_size);
        for (std::size_t i = 0; i < fft_size; ++i) {
            hann_window[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * i / (fft_size - 1)));
        }
    }

    std::vector<std::complex<float>> spectrum(fft_size);
    for (std::size_t i = 0; i < fft_size; ++i) {
        spectrum[i] = {samples[i] * hann_window[i], 0.0f};
    }

    fft(spectrum);

    auto band_energy = [&](std::size_t start_bin, std::size_t end_bin) {
        end_bin = std::min(end_bin, fft_size / 2 - 1);
        float sum = 0.0f;
        for (std::size_t b = start_bin; b <= end_bin; ++b) {
            sum += std::abs(spectrum[b]);
        }
        return sum / static_cast<float>(end_bin - start_bin + 1);
    };

    float raw_b = band_energy(1, 4);
    float raw_m = band_energy(5, 42);
    float raw_t = band_energy(43, 256);

    static float peak_b = 0.05f;
    static float peak_m = 0.05f;
    static float peak_t = 0.02f;
    static float peak_spec = 0.05f;

    peak_b = std::max(raw_b, peak_b * 0.992f + 0.0005f);
    peak_m = std::max(raw_m, peak_m * 0.992f + 0.0005f);
    peak_t = std::max(raw_t, peak_t * 0.992f + 0.0002f);

    audio.bass.store(std::clamp(raw_b / peak_b, 0.0f, 1.0f));
    audio.mid.store(std::clamp(raw_m / peak_m, 0.0f, 1.0f));
    audio.treble.store(std::clamp(raw_t / peak_t, 0.0f, 1.0f));

    std::array<float, AURORA_POINTS> local_wave{};
    std::array<float, AURORA_POINTS> local_spec{};

    float max_mag = 0.001f;
    for (std::size_t i = 0; i < AURORA_POINTS; ++i) {
        std::size_t sample_idx = (i * (fft_size - 1)) / (AURORA_POINTS - 1);
        local_wave[i] = std::clamp(samples[sample_idx] * 2.8f, -1.0f, 1.0f);

        float norm = static_cast<float>(i) / static_cast<float>(AURORA_POINTS - 1);
        std::size_t bin = std::min(
            fft_size / 2 - 1,
            1 + static_cast<std::size_t>(std::pow(norm, 1.7f) * 210.0f)
        );
        float mag = std::abs(spectrum[bin]);
        local_spec[i] = mag;
        if (mag > max_mag) max_mag = mag;
    }

    peak_spec = std::max(max_mag, peak_spec * 0.99f + 0.001f);
    for (float& v : local_spec) {
        v = std::clamp(v / peak_spec, 0.0f, 1.0f);
    }

    {
        std::scoped_lock lock(audio.wave_mtx);
        audio.waveform = local_wave;
        audio.spectrum_env = local_spec;
    }
}

int main() {
    AudioData audio_data;
    std::jthread audio_thread(audio_capture_worker, std::ref(audio_data));

    constexpr unsigned int W = 900;
    constexpr unsigned int H = 760;

    sf::ContextSettings settings;
    settings.antialiasingLevel = 8;

    sf::RenderWindow window(
        sf::VideoMode(W, H),
        "C++23 & SFML Revontuli-Joulukuusi ja Talvimaisema (Esc / Q sulkee)",
        sf::Style::Titlebar | sf::Style::Close,
        settings
    );
    window.setFramerateLimit(60);

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> phase_dist(0.0f, 2.0f * std::numbers::pi_v<float>);

    sf::VertexArray sky(sf::Quads, 4);
    sky[0] = {{0.f, 0.f}, {5, 9, 24}};
    sky[1] = {{static_cast<float>(W), 0.f}, {5, 9, 24}};
    sky[2] = {{static_cast<float>(W), static_cast<float>(H)}, {18, 34, 66}};
    sky[3] = {{0.f, static_cast<float>(H)}, {18, 34, 66}};

    std::vector<BgStar> bg_stars(75);
    for (auto& st : bg_stars) {
        st.pos = {dist01(rng) * W, dist01(rng) * (H * 0.72f)};
        st.radius = 1.0f + dist01(rng) * 1.5f;
        st.phase = phase_dist(rng);
    }

    ShootingStar shooting_star;
    float next_meteor_timer = 1.8f;

    auto spawn_shooting_star = [&]() {
        bool left_to_right = (dist01(rng) < 0.65f);
        float start_x = left_to_right
            ? (60.f + dist01(rng) * (W * 0.45f))
            : (W * 0.45f + dist01(rng) * (W * 0.48f));
        float start_y = 25.f + dist01(rng) * 120.f;

        float angle_deg = left_to_right
            ? (20.f + dist01(rng) * 16.f)
            : (144.f + dist01(rng) * 16.f);
        float angle_rad = angle_deg * (std::numbers::pi_v<float> / 180.f);
        float speed = 460.f + dist01(rng) * 190.f;

        shooting_star.active = true;
        shooting_star.pos = {start_x, start_y};
        shooting_star.vel = {std::cos(angle_rad) * speed, std::sin(angle_rad) * speed};
        shooting_star.age = 0.f;
        shooting_star.max_age = 0.95f + dist01(rng) * 0.45f;
        shooting_star.tail_length = 140.f + dist01(rng) * 80.f;
    };

    constexpr float center_x = W * 0.5f;
    constexpr float tree_top_y = 135.f;
    constexpr float tree_bottom_y = 590.f;
    constexpr float base_half_width = 215.f;
    constexpr int tiers = 5;

    struct PaletteEntry {
        sf::Color color;
        BandType band;
    };
    const std::vector<PaletteEntry> palette = {
        {{255, 45, 55},   BandType::Bass},
        {{255, 195, 25},  BandType::Bass},
        {{55, 165, 255},  BandType::Mid},
        {{50, 235, 130},  BandType::Mid},
        {{255, 75, 215},  BandType::Treble},
        {{255, 250, 235}, BandType::Treble}
    };
    std::uniform_int_distribution<std::size_t> pal_dist(0, palette.size() - 1);
    std::uniform_real_distribution<float> speed_dist(1.8f, 4.2f);

    std::vector<Led> leds;
    for (int i = 0; i < 105; ++i) {
        float norm_y = std::sqrt(dist01(rng));
        float y = tree_top_y + 25.f + norm_y * (tree_bottom_y - tree_top_y - 35.f);
        float max_dx = ((y - tree_top_y) / (tree_bottom_y - tree_top_y)) * (base_half_width * 0.86f);
        float dx = (dist01(rng) * 2.0f - 1.0f) * max_dx;
        const auto& pe = palette[pal_dist(rng)];

        leds.push_back(Led{
            .pos = {center_x + dx, y},
            .base_color = pe.color,
            .band = pe.band,
            .phase = phase_dist(rng),
            .speed = speed_dist(rng),
            .radius = 4.3f + dist01(rng) * 1.4f
        });
    }

    auto snow_surface_y = [&](float x) -> float {
        return 645.f + 12.f * std::sin(x * 0.009f) + 6.f * std::cos(x * 0.022f);
    };

    const std::vector<BgTree> bg_trees = {
        {735.f, 138.f, 44.f, 4, true},
        {830.f, 122.f, 38.f, 4, true},
        {692.f, 168.f, 54.f, 4, false},
        {786.f, 195.f, 62.f, 5, false}
    };

    constexpr float cabin_x = 85.f;
    constexpr float cabin_w = 104.f;
    constexpr float cabin_h = 54.f;
    const float cabin_base_y = snow_surface_y(cabin_x + cabin_w * 0.5f) + 6.f;
    const float cabin_top_y = cabin_base_y - cabin_h;
    const float roof_peak_x = cabin_x + cabin_w * 0.5f;
    const float roof_peak_y = cabin_top_y - 34.f;
    const sf::Vector2f chimney_top{cabin_x + cabin_w * 0.72f, roof_peak_y + 2.f};

    std::vector<SmokePuff> smoke_puffs;
    float smoke_spawn_timer = 0.f;

    constexpr int snow_segments = 128;
    sf::VertexArray snow_drift(sf::TriangleStrip, (snow_segments + 1) * 2);
    sf::VertexArray snow_reflection(sf::TriangleStrip, (snow_segments + 1) * 2);
    sf::VertexArray snow_rim(sf::LineStrip, snow_segments + 1);

    const std::vector<GiftBox> gifts = {
        {center_x - 165.f, 656.f, 68.f, 52.f, {205, 32, 42},  {255, 210, 45}},
        {center_x - 90.f,  660.f, 52.f, 38.f, {35, 115, 220}, {245, 248, 255}},
        {center_x + 38.f,  658.f, 64.f, 48.f, {225, 160, 25}, {195, 28, 38}},
        {center_x + 112.f, 654.f, 54.f, 40.f, {145, 50, 190}, {110, 235, 155}}
    };

    std::vector<Snowflake> snowflakes(160);
    for (auto& s : snowflakes) {
        s.pos = {dist01(rng) * W, dist01(rng) * H};
        s.vy = 35.f + dist01(rng) * 65.f;
        s.radius = 1.4f + dist01(rng) * 2.4f;
        s.drift_speed = 0.8f + dist01(rng) * 1.5f;
        s.phase = phase_dist(rng);
        s.alpha = static_cast<std::uint8_t>(130 + dist01(rng) * 120);
    }

    sf::ConvexShape star_shape = create_star(25.f, 10.5f);
    star_shape.setPosition(center_x, tree_top_y - 4.f);

    float smooth_bass = 0.0f, smooth_mid = 0.0f, smooth_treble = 0.0f;
    std::array<float, AURORA_POINTS> smooth_wave{};
    std::array<float, AURORA_POINTS> smooth_spec{};

    sf::Clock clock;

    while (window.isOpen()) {
        sf::Event event{};
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed ||
               (event.type == sf::Event::KeyPressed &&
               (event.key.code == sf::Keyboard::Escape || event.key.code == sf::Keyboard::Q))) {
                window.close();
            }
        }

        float dt = clock.restart().asSeconds();
        static float total_time = 0.f;
        total_time += dt;

        float target_b = audio_data.bass.load();
        float target_m = audio_data.mid.load();
        float target_t = audio_data.treble.load();

        auto smooth = [](float current, float target, float attack, float release) {
            float k = (target > current) ? attack : release;
            return current + (target - current) * k;
        };
        smooth_bass   = smooth(smooth_bass, target_b, 0.55f, 0.14f);
        smooth_mid    = smooth(smooth_mid, target_m, 0.45f, 0.16f);
        smooth_treble = smooth(smooth_treble, target_t, 0.65f, 0.22f);

        {
            std::scoped_lock lock(audio_data.wave_mtx);
            for (std::size_t i = 0; i < AURORA_POINTS; ++i) {
                smooth_wave[i] = smooth(smooth_wave[i], audio_data.waveform[i], 0.60f, 0.25f);
                smooth_spec[i] = smooth(smooth_spec[i], audio_data.spectrum_env[i], 0.50f, 0.15f);
            }
        }

        float total_energy = smooth_bass + smooth_mid + smooth_treble;
        bool has_music = audio_data.active.load() && (total_energy > 0.05f);

        // Tähdenlento
        if (!shooting_star.active) {
            float timer_step = dt * (has_music && smooth_treble > 0.65f ? 1.6f : 1.0f);
            next_meteor_timer -= timer_step;
            if (next_meteor_timer <= 0.f) {
                spawn_shooting_star();
                next_meteor_timer = 3.5f + dist01(rng) * 5.0f;
            }
        } else {
            shooting_star.age += dt;
            shooting_star.pos += shooting_star.vel * dt;
            if (shooting_star.age >= shooting_star.max_age) {
                shooting_star.active = false;
            }
        }

        // Savupiipun savu
        smoke_spawn_timer += dt;
        if (smoke_spawn_timer >= 0.22f) {
            smoke_spawn_timer = 0.f;
            smoke_puffs.push_back(SmokePuff{
                .pos = {chimney_top.x + 6.f, chimney_top.y - 2.f},
                .age = 0.f,
                .max_age = 4.6f + dist01(rng) * 1.4f,
                .start_radius = 4.5f + dist01(rng) * 2.0f,
                .drift_offset = phase_dist(rng)
            });
        }

        float smoke_lift = has_music ? (26.f + smooth_bass * 18.f) : 28.f;
        for (auto& p : smoke_puffs) {
            p.age += dt;
            p.pos.y -= smoke_lift * dt;
            p.pos.x += (11.f + 6.f * std::sin(total_time * 1.4f + p.drift_offset)) * dt;
        }
        std::erase_if(smoke_puffs, [](const SmokePuff& p) { return p.age >= p.max_age; });

        for (auto& s : snowflakes) {
            s.pos.y += s.vy * dt;
            s.pos.x += std::sin(total_time * s.drift_speed + s.phase) * 24.f * dt;
            if (s.pos.x < 0.f) s.pos.x += W;
            if (s.pos.x >= W) s.pos.x -= W;
            if (s.pos.y >= snow_surface_y(s.pos.x)) {
                s.pos.y = -5.f;
                s.pos.x = dist01(rng) * W;
            }
        }

        window.clear();
        window.draw(sky);

        // Taivaan tähdet
        for (const auto& st : bg_stars) {
            float twinkle = 0.45f + 0.55f * std::sin(total_time * 2.2f + st.phase);
            sf::CircleShape dot(st.radius);
            dot.setOrigin(st.radius, st.radius);
            dot.setPosition(st.pos);
            dot.setFillColor({235, 242, 255, static_cast<std::uint8_t>(twinkle * 210)});
            window.draw(dot);
        }

        // Tähdenlento
        if (shooting_star.active) {
            float life = shooting_star.age / shooting_star.max_age;
            float envelope = std::sin(std::min(1.0f, life * 4.5f) * (std::numbers::pi_v<float> * 0.5f))
                           * std::pow(1.0f - life, 1.3f);

            float speed_len = std::hypot(shooting_star.vel.x, shooting_star.vel.y);
            sf::Vector2f dir = {shooting_star.vel.x / speed_len, shooting_star.vel.y / speed_len};
            sf::Vector2f perp = {-dir.y, dir.x};

            float current_len = shooting_star.tail_length * std::sin(life * std::numbers::pi_v<float>);
            constexpr int tail_segs = 24;

            sf::VertexArray outer_trail(sf::TriangleStrip, (tail_segs + 1) * 2);
            sf::VertexArray inner_trail(sf::TriangleStrip, (tail_segs + 1) * 2);

            for (int i = 0; i <= tail_segs; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(tail_segs);
                sf::Vector2f pt = shooting_star.pos - dir * (t * current_len);

                float fade = std::pow(1.0f - t, 1.8f) * envelope;
                float outer_w = 5.5f * (1.0f - t * 0.85f);
                float inner_w = 1.8f * (1.0f - t * 0.90f);

                sf::Color outer_col{140, 210, 255, static_cast<std::uint8_t>(95.f * fade)};
                sf::Color inner_col{245, 252, 255, static_cast<std::uint8_t>(220.f * fade)};

                outer_trail[i * 2]     = {pt + perp * outer_w, outer_col};
                outer_trail[i * 2 + 1] = {pt - perp * outer_w, outer_col};

                inner_trail[i * 2]     = {pt + perp * inner_w, inner_col};
                inner_trail[i * 2 + 1] = {pt - perp * inner_w, inner_col};
            }

            window.draw(outer_trail, sf::BlendAdd);
            window.draw(inner_trail, sf::BlendAdd);

            sf::CircleShape head_glow(9.f);
            head_glow.setOrigin(9.f, 9.f);
            head_glow.setPosition(shooting_star.pos);
            head_glow.setFillColor({180, 230, 255, static_cast<std::uint8_t>(140.f * envelope)});
            window.draw(head_glow, sf::BlendAdd);

            sf::CircleShape head_core(2.6f);
            head_core.setOrigin(2.6f, 2.6f);
            head_core.setPosition(shooting_star.pos);
            head_core.setFillColor({255, 255, 255, static_cast<std::uint8_t>(255.f * envelope)});
            window.draw(head_core, sf::BlendAdd);
        }

        // Revontulet
        sf::VertexArray aurora_curtain(sf::TriangleStrip, AURORA_POINTS * 2);
        sf::VertexArray aurora_edge(sf::LineStrip, AURORA_POINTS);

        for (std::size_t i = 0; i < AURORA_POINTS; ++i) {
            float norm_x = static_cast<float>(i) / static_cast<float>(AURORA_POINTS - 1);
            float x = norm_x * static_cast<float>(W);

            float sky_arc = 155.f
                          + 32.f * std::sin(norm_x * 4.2f + total_time * 0.45f)
                          + 16.f * std::cos(norm_x * 9.0f - total_time * 0.7f);

            float wave_offset = has_music
                ? (smooth_wave[i] * 52.f)
                : (8.f * std::sin(norm_x * 14.0f + total_time * 1.8f));

            float bottom_y = sky_arc + wave_offset;

            float spec_boost = has_music ? (smooth_spec[i] * 115.f + smooth_bass * 35.f) : 25.f;
            float ray_shimmer = 18.f * std::sin(norm_x * 28.0f + total_time * 2.4f);
            float top_y = std::max(10.f, bottom_y - (55.f + spec_boost + ray_shimmer));

            float edge_fade = std::sin(norm_x * std::numbers::pi_v<float>);
            float intensity = has_music
                ? std::clamp(0.35f + 0.65f * smooth_spec[i] + 0.25f * smooth_mid, 0.25f, 1.0f)
                : 0.32f;

            std::uint8_t bot_alpha = static_cast<std::uint8_t>(145 * intensity * edge_fade);
            std::uint8_t top_alpha = static_cast<std::uint8_t>(12 * intensity * edge_fade);

            sf::Color bottom_col{
                static_cast<std::uint8_t>(30 + 60 * smooth_treble),
                245,
                static_cast<std::uint8_t>(145 + 80 * smooth_mid),
                bot_alpha
            };

            sf::Color top_col{
                static_cast<std::uint8_t>(140 + 110 * smooth_treble),
                50,
                220,
                top_alpha
            };

            aurora_curtain[i * 2]     = {{x, top_y}, top_col};
            aurora_curtain[i * 2 + 1] = {{x, bottom_y}, bottom_col};

            aurora_edge[i] = {{x, bottom_y}, {
                120, 255, 200,
                static_cast<std::uint8_t>(210 * intensity * edge_fade)
            }};
        }

        window.draw(aurora_curtain, sf::BlendAdd);
        window.draw(aurora_edge, sf::BlendAdd);

        // Horisontin siluettikuuset
        for (const auto& bt : bg_trees) {
            float base_y = snow_surface_y(bt.x) + (bt.far_layer ? 2.f : 8.f);
            float top_y = base_y - bt.height;

            sf::Color silhouette_col = bt.far_layer
                ? sf::Color{14, 28, 52}
                : sf::Color{10, 21, 40};
            sf::Color snow_rim_col = bt.far_layer
                ? sf::Color{120, 155, 195, 110}
                : sf::Color{145, 180, 215, 145};

            sf::RectangleShape b_trunk({bt.half_width * 0.22f, 18.f});
            b_trunk.setOrigin(bt.half_width * 0.11f, 0.f);
            b_trunk.setPosition(bt.x, base_y - 10.f);
            b_trunk.setFillColor(silhouette_col);
            window.draw(b_trunk);

            for (int t = bt.tiers - 1; t >= 0; --t) {
                float f0 = static_cast<float>(t) / bt.tiers;
                float f1 = static_cast<float>(t + 1) / bt.tiers;
                float ty = top_y + f0 * (bt.height * 0.80f);
                float by = top_y + f1 * (bt.height * 0.94f);
                float hw = f1 * bt.half_width;

                sf::ConvexShape snow_cap(3);
                snow_cap.setPoint(0, {bt.x, ty - 2.f});
                snow_cap.setPoint(1, {bt.x - hw * 1.02f, by - 1.f});
                snow_cap.setPoint(2, {bt.x + hw * 1.02f, by - 1.f});
                snow_cap.setFillColor(snow_rim_col);
                window.draw(snow_cap);

                sf::ConvexShape b_tier(3);
                b_tier.setPoint(0, {bt.x, ty + 1.5f});
                b_tier.setPoint(1, {bt.x - hw, by + 1.5f});
                b_tier.setPoint(2, {bt.x + hw, by + 1.5f});
                b_tier.setFillColor(silhouette_col);
                window.draw(b_tier);
            }
        }

        // Talvimökki ja savupiipun savu
        for (const auto& p : smoke_puffs) {
            float life = p.age / p.max_age;
            float r = p.start_radius * (1.0f + life * 2.8f);
            float alpha = std::sin(std::min(1.0f, life * 4.0f) * (std::numbers::pi_v<float> * 0.5f))
                        * (1.0f - life) * 85.f;

            sf::CircleShape puff(r);
            puff.setOrigin(r, r);
            puff.setPosition(p.pos);
            puff.setFillColor({205, 218, 238, static_cast<std::uint8_t>(alpha)});
            window.draw(puff);
        }

        sf::RectangleShape chimney({12.f, 24.f});
        chimney.setPosition(chimney_top);
        chimney.setFillColor({68, 42, 38});
        window.draw(chimney);

        sf::RectangleShape cabin_body({cabin_w, cabin_h});
        cabin_body.setPosition(cabin_x, cabin_top_y);
        cabin_body.setFillColor({96, 28, 28});
        cabin_body.setOutlineThickness(1.f);
        cabin_body.setOutlineColor({62, 18, 18});
        window.draw(cabin_body);

        sf::ConvexShape gable(3);
        gable.setPoint(0, {roof_peak_x, roof_peak_y});
        gable.setPoint(1, {cabin_x, cabin_top_y});
        gable.setPoint(2, {cabin_x + cabin_w, cabin_top_y});
        gable.setFillColor({84, 24, 24});
        window.draw(gable);

        sf::ConvexShape roof_under(4);
        roof_under.setPoint(0, {roof_peak_x, roof_peak_y - 3.f});
        roof_under.setPoint(1, {cabin_x + cabin_w + 10.f, cabin_top_y + 4.f});
        roof_under.setPoint(2, {cabin_x + cabin_w + 8.f, cabin_top_y + 8.f});
        roof_under.setPoint(3, {cabin_x - 8.f, cabin_top_y + 8.f});
        roof_under.setFillColor({42, 48, 62});
        window.draw(roof_under);

        sf::ConvexShape roof_snow(6);
        roof_snow.setPoint(0, {roof_peak_x, roof_peak_y - 8.f});
        roof_snow.setPoint(1, {cabin_x + cabin_w + 11.f, cabin_top_y + 1.f});
        roof_snow.setPoint(2, {cabin_x + cabin_w + 10.f, cabin_top_y + 5.f});
        roof_snow.setPoint(3, {roof_peak_x, roof_peak_y - 2.f});
        roof_snow.setPoint(4, {cabin_x - 10.f, cabin_top_y + 5.f});
        roof_snow.setPoint(5, {cabin_x - 11.f, cabin_top_y + 1.f});
        roof_snow.setFillColor({228, 240, 255});
        window.draw(roof_snow);

        sf::RectangleShape door({16.f, 32.f});
        door.setPosition(cabin_x + 16.f, cabin_base_y - 32.f);
        door.setFillColor({58, 32, 22});
        window.draw(door);

        float window_warmth = has_music
            ? (0.78f + 0.22f * smooth_mid)
            : (0.88f + 0.12f * std::sin(total_time * 1.7f));

        const std::array<float, 2> win_xs = {cabin_x + 44.f, cabin_x + 73.f};
        for (float wx : win_xs) {
            float wy = cabin_top_y + 16.f;
            float ww = 18.f;
            float wh = 18.f;

            sf::CircleShape win_halo(22.f);
            win_halo.setOrigin(22.f, 22.f);
            win_halo.setPosition(wx + ww * 0.5f, wy + wh * 0.5f);
            win_halo.setFillColor({255, 195, 70, static_cast<std::uint8_t>(55 * window_warmth)});
            window.draw(win_halo, sf::BlendAdd);

            sf::RectangleShape win({ww, wh});
            win.setPosition(wx, wy);
            win.setFillColor({
                255,
                static_cast<std::uint8_t>(205 + 35 * window_warmth),
                static_cast<std::uint8_t>(85 + 45 * window_warmth)
            });
            win.setOutlineThickness(1.5f);
            win.setOutlineColor({225, 235, 245});
            window.draw(win);

            sf::RectangleShape v_bar({1.5f, wh});
            v_bar.setPosition(wx + ww * 0.5f - 0.75f, wy);
            v_bar.setFillColor({70, 35, 25});
            window.draw(v_bar);

            sf::RectangleShape h_bar({ww, 1.5f});
            h_bar.setPosition(wx, wy + wh * 0.5f - 0.75f);
            h_bar.setFillColor({70, 35, 25});
            window.draw(h_bar);
        }

        // Etualan joulukuusi
        sf::RectangleShape trunk({38.f, 75.f});
        trunk.setOrigin(19.f, 0.f);
        trunk.setPosition(center_x, tree_bottom_y - 8.f);
        trunk.setFillColor({88, 52, 24});
        window.draw(trunk);

        for (int tier = tiers - 1; tier >= 0; --tier) {
            float t0 = static_cast<float>(tier) / tiers;
            float t1 = static_cast<float>(tier + 1) / tiers;
            float top_y = tree_top_y + t0 * (tree_bottom_y - tree_top_y) * 0.82f;
            float bot_y = tree_top_y + t1 * (tree_bottom_y - tree_top_y);
            float half_w = t1 * base_half_width;

            sf::ConvexShape shadow(3);
            shadow.setPoint(0, {center_x, top_y + 6.f});
            shadow.setPoint(1, {center_x - half_w * 0.96f, bot_y + 8.f});
            shadow.setPoint(2, {center_x + half_w * 0.96f, bot_y + 8.f});
            shadow.setFillColor({10, 48, 22});
            window.draw(shadow);

            sf::ConvexShape branch(3);
            branch.setPoint(0, {center_x, top_y});
            branch.setPoint(1, {center_x - half_w, bot_y});
            branch.setPoint(2, {center_x + half_w, bot_y});
            std::uint8_t green_g = static_cast<std::uint8_t>(92 + (tiers - tier) * 8);
            branch.setFillColor({18, green_g, 38});
            window.draw(branch);
        }

        // Lumikinos ja väriheijastukset
        float bass_glow = has_music ? smooth_bass : (0.28f + 0.12f * std::sin(total_time * 2.0f));
        float aurora_glow = has_music ? (0.35f + 0.65f * smooth_mid) : 0.30f;

        for (int i = 0; i <= snow_segments; ++i) {
            float norm_x = static_cast<float>(i) / static_cast<float>(snow_segments);
            float x = norm_x * static_cast<float>(W);
            float top_y = snow_surface_y(x);

            float dist_center = std::abs(x - center_x) / (W * 0.5f);
            float tree_proximity = std::exp(-dist_center * dist_center * 5.5f);

            std::size_t spec_idx = std::min(AURORA_POINTS - 1,
                static_cast<std::size_t>(norm_x * (AURORA_POINTS - 1)));
            float local_aurora = has_music ? smooth_spec[spec_idx] : 0.25f;

            std::uint8_t base_r = static_cast<std::uint8_t>(std::clamp(
                215.f + 35.f * tree_proximity * bass_glow, 180.f, 255.f));
            std::uint8_t base_g = static_cast<std::uint8_t>(std::clamp(
                232.f + 22.f * (aurora_glow * 0.6f + local_aurora * 0.4f), 200.f, 255.f));
            std::uint8_t base_b = static_cast<std::uint8_t>(std::clamp(
                248.f + 7.f * aurora_glow, 220.f, 255.f));

            snow_drift[i * 2]     = {{x, top_y}, {base_r, base_g, base_b}};
            snow_drift[i * 2 + 1] = {{x, static_cast<float>(H)}, {165, 192, 224}};

            float shimmer = 0.85f + 0.15f * std::sin(norm_x * 16.0f + total_time * 1.8f);
            float refl_r = (115.f * tree_proximity * bass_glow + 35.f * smooth_treble * local_aurora) * shimmer;
            float refl_g = (95.f * aurora_glow * (0.45f + 0.55f * local_aurora) + 45.f * tree_proximity * bass_glow) * shimmer;
            float refl_b = (75.f * aurora_glow * (0.4f + 0.6f * local_aurora)) * shimmer;
            float refl_a = std::clamp(35.f + 85.f * (tree_proximity * bass_glow * 0.7f + local_aurora * 0.5f), 25.f, 130.f);

            float refl_depth = 38.f + 32.f * tree_proximity * bass_glow + 18.f * local_aurora;

            snow_reflection[i * 2] = {{x, top_y}, {
                static_cast<std::uint8_t>(std::min(255.f, refl_r)),
                static_cast<std::uint8_t>(std::min(255.f, refl_g)),
                static_cast<std::uint8_t>(std::min(255.f, refl_b)),
                static_cast<std::uint8_t>(refl_a)
            }};
            snow_reflection[i * 2 + 1] = {{x, top_y + refl_depth}, {0, 0, 0, 0}};

            float rim_intensity = std::clamp(0.35f + 0.45f * tree_proximity * bass_glow + 0.35f * smooth_treble, 0.25f, 1.0f);
            snow_rim[i] = {{x, top_y}, {
                static_cast<std::uint8_t>(std::min(255.f, 180.f + 75.f * tree_proximity * bass_glow)),
                static_cast<std::uint8_t>(std::min(255.f, 225.f + 30.f * aurora_glow)),
                255,
                static_cast<std::uint8_t>(160.f * rim_intensity)
            }};
        }

        window.draw(snow_drift);
        window.draw(snow_reflection, sf::BlendAdd);
        window.draw(snow_rim, sf::BlendAdd);

        // Mökin ikkunoiden valokajo lumeen
        sf::CircleShape cabin_snow_glow(58.f);
        cabin_snow_glow.setScale(1.45f, 0.35f);
        cabin_snow_glow.setOrigin(58.f, 58.f);
        cabin_snow_glow.setPosition(cabin_x + cabin_w * 0.62f, cabin_base_y + 8.f);
        cabin_snow_glow.setFillColor({255, 185, 65, static_cast<std::uint8_t>(48 * window_warmth)});
        window.draw(cabin_snow_glow, sf::BlendAdd);

        // Lahjapaketit
        for (const auto& g : gifts) {
            float top_y = g.ground_y - g.h;

            sf::CircleShape gift_glow(g.w * 0.65f);
            gift_glow.setScale(1.2f, 0.32f);
            gift_glow.setOrigin(g.w * 0.65f, g.w * 0.65f);
            gift_glow.setPosition(g.x + g.w * 0.5f, g.ground_y + 3.f);
            gift_glow.setFillColor({
                g.box_color.r, g.box_color.g, g.box_color.b,
                static_cast<std::uint8_t>(38 + 32 * bass_glow)
            });
            window.draw(gift_glow, sf::BlendAdd);

            sf::RectangleShape box({g.w, g.h});
            box.setPosition(g.x, top_y);
            box.setFillColor(g.box_color);
            window.draw(box);

            sf::RectangleShape v_rib({8.f, g.h});
            v_rib.setPosition(g.x + g.w * 0.5f - 4.f, top_y);
            v_rib.setFillColor(g.ribbon_color);
            window.draw(v_rib);

            sf::RectangleShape h_rib({g.w, 8.f});
            h_rib.setPosition(g.x, top_y + g.h * 0.5f - 4.f);
            h_rib.setFillColor(g.ribbon_color);
            window.draw(h_rib);

            float bow_cx = g.x + g.w * 0.5f;
            for (float side : {-1.f, 1.f}) {
                sf::CircleShape loop(6.f);
                loop.setScale(1.3f, 0.8f);
                loop.setOrigin(6.f, 6.f);
                loop.setPosition(bow_cx + side * 7.f, top_y - 4.f);
                loop.setFillColor(sf::Color::Transparent);
                loop.setOutlineThickness(2.5f);
                loop.setOutlineColor(g.ribbon_color);
                window.draw(loop);
            }
        }

        // Ledit
        for (const auto& led : leds) {
            float idle_wave = 0.5f + 0.5f * std::sin(total_time * led.speed + led.phase);
            float idle_intensity = 0.14f + 0.45f * (idle_wave * idle_wave);

            float audio_val = 0.0f;
            if (led.band == BandType::Bass)   audio_val = smooth_bass;
            if (led.band == BandType::Mid)    audio_val = smooth_mid;
            if (led.band == BandType::Treble) audio_val = smooth_treble;

            float intensity = has_music
                ? std::clamp(0.12f + 0.95f * std::pow(audio_val, 1.15f), 0.10f, 1.0f)
                : idle_intensity;

            float halo_r = led.radius * (2.4f + 2.0f * intensity);
            sf::CircleShape halo(halo_r);
            halo.setOrigin(halo_r, halo_r);
            halo.setPosition(led.pos);
            halo.setFillColor({
                led.base_color.r, led.base_color.g, led.base_color.b,
                static_cast<std::uint8_t>(65 * intensity)
            });
            window.draw(halo, sf::BlendAdd);

            sf::CircleShape core(led.radius * (0.9f + 0.25f * intensity));
            core.setOrigin(core.getRadius(), core.getRadius());
            core.setPosition(led.pos);
            core.setFillColor({
                static_cast<std::uint8_t>(std::min(255.f, led.base_color.r * intensity + 70.f * intensity)),
                static_cast<std::uint8_t>(std::min(255.f, led.base_color.g * intensity + 70.f * intensity)),
                static_cast<std::uint8_t>(std::min(255.f, led.base_color.b * intensity + 70.f * intensity))
            });
            window.draw(core);
        }

        // Latvatähti
        float star_pulse = has_music ? (0.45f + 0.55f * smooth_bass)
                                     : (0.78f + 0.22f * std::sin(total_time * 3.0f));
        sf::CircleShape star_glow(52.f * star_pulse);
        star_glow.setOrigin(52.f * star_pulse, 52.f * star_pulse);
        star_glow.setPosition(star_shape.getPosition());
        star_glow.setFillColor({255, 220, 60, static_cast<std::uint8_t>(85 * star_pulse)});
        window.draw(star_glow, sf::BlendAdd);

        star_shape.setScale(0.92f + 0.18f * star_pulse, 0.92f + 0.18f * star_pulse);
        star_shape.setFillColor({255, 235, 75});
        window.draw(star_shape);

        // Etualan lumisade
        for (const auto& s : snowflakes) {
            sf::CircleShape flake(s.radius);
            flake.setOrigin(s.radius, s.radius);
            flake.setPosition(s.pos);
            flake.setFillColor({245, 250, 255, s.alpha});
            window.draw(flake);
        }

        window.display();
    }

    return 0;
}
