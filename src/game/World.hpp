#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <cstdint>
#include <memory>

// Isometric tile system for roguelike dungeons
class World {
public:
    struct Tile {
        uint16_t tileId{ 0 };     // Which tile graphic to use
        uint8_t  flags{ 0 };       // Walkable, etc (bitmask)

        enum Flags : uint8_t {
            Walkable = 1 << 0,
            Transparent = 1 << 1,
            Water = 1 << 2,
            Lava = 1 << 3
        };
    };

    // Camera for smooth scrolling
    struct Camera {
        float x{ 0.f };
        float y{ 0.f };
        float zoom{ 1.f };
    };

public:
    World();
    ~World() = default;

    // Initialize from seed (procedural generation)
    void generate(uint32_t seed, int width, int height);

    // Manual tile placement for testing
    void setTile(int x, int y, uint16_t tileId, uint8_t flags = Tile::Walkable);
    const Tile* getTile(int x, int y) const;

    // Rendering
    void render(sf::RenderWindow& window, const Camera& cam) const;

    // Coordinate conversion
    sf::Vector2f worldToScreen(int tileX, int tileY, const Camera& cam) const;
    sf::Vector2i screenToWorld(float screenX, float screenY, const Camera& cam) const;

    // Collision
    bool isWalkable(int x, int y) const;

    // Getters
    int width() const { return m_width; }
    int height() const { return m_height; }

    // Tileset management
    bool loadTileset(const std::string& path, int tileWidth, int tileHeight);

private:
    int m_width{ 0 };
    int m_height{ 0 };
    std::vector<Tile> m_tiles;

    // Isometric constants
    static constexpr int TILE_WIDTH = 64;   // Base tile width in pixels
    static constexpr int TILE_HEIGHT = 32;  // Base tile height in pixels

    // Tileset texture
    sf::Texture m_tileset;
    std::vector<sf::IntRect> m_tileRects;
    int m_tilesetTileW{ 64 };
    int m_tilesetTileH{ 32 };

    // Helper
    int index(int x, int y) const { return y * m_width + x; }
    bool inBounds(int x, int y) const { return x >= 0 && x < m_width && y >= 0 && y < m_height; }
};