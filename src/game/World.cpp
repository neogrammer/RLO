#include "World.hpp"
#include <cmath>
#include <random>
#include <iostream>
#include <SFML/Graphics.hpp>

World::World() {
    // Default constructor
}

void World::generate(uint32_t seed, int width, int height) {
    m_width = width;
    m_height = height;
    m_tiles.resize(width * height);

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> floorDist(0, 3); // 0-3 for floor tile variety
    std::uniform_int_distribution<int> wallChance(0, 100);

    // Simple generation: mostly floor, some walls
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Tile t{};

            // Border walls
            if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
                t.tileId = 10; // Wall tile
                t.flags = 0;   // Not walkable
            }
            // Random interior walls (10% chance)
            else if (wallChance(rng) < 10) {
                t.tileId = 10;
                t.flags = 0;
            }
            // Floor tiles
            else {
                t.tileId = floorDist(rng); // 0-3 for variety
                t.flags = Tile::Walkable | Tile::Transparent;
            }

            m_tiles[index(x, y)] = t;
        }
    }

    std::cout << "[World] Generated " << width << "x" << height << " world (seed=" << seed << ")\n";
}

void World::setTile(int x, int y, uint16_t tileId, uint8_t flags) {
    if (!inBounds(x, y)) return;
    m_tiles[index(x, y)].tileId = tileId;
    m_tiles[index(x, y)].flags = flags;
}

const World::Tile* World::getTile(int x, int y) const {
    if (!inBounds(x, y)) return nullptr;
    return &m_tiles[index(x, y)];
}

bool World::isWalkable(int x, int y) const {
    const Tile* t = getTile(x, y);
    return t && (t->flags & Tile::Walkable);
}

sf::Vector2f World::worldToScreen(int tileX, int tileY, const Camera& cam) const {
    // Isometric projection
    float screenX = (tileX - tileY) * (TILE_WIDTH / 2.0f);
    float screenY = (tileX + tileY) * (TILE_HEIGHT / 2.0f);

    // Apply camera
    screenX = (screenX - cam.x) * cam.zoom;
    screenY = (screenY - cam.y) * cam.zoom;

    return { screenX, screenY };
}

sf::Vector2i World::screenToWorld(float screenX, float screenY, const Camera& cam) const {
    // Reverse camera transform
    float wx = screenX / cam.zoom + cam.x;
    float wy = screenY / cam.zoom + cam.y;

    // Inverse isometric projection
    float tileX = (wx / (TILE_WIDTH / 2.0f) + wy / (TILE_HEIGHT / 2.0f)) / 2.0f;
    float tileY = (wy / (TILE_HEIGHT / 2.0f) - wx / (TILE_WIDTH / 2.0f)) / 2.0f;

    return { static_cast<int>(std::floor(tileX)), static_cast<int>(std::floor(tileY)) };
}

bool World::loadTileset(const std::string& path, int tileWidth, int tileHeight) {
    if (!m_tileset.loadFromFile(path)) {
        std::cerr << "[World] Failed to load tileset: " << path << "\n";
        return false;
    }

    m_tilesetTileW = tileWidth;
    m_tilesetTileH = tileHeight;

    // Build tile rects for quick lookup
    const sf::Vector2u texSize = m_tileset.getSize();
    const int tilesX = texSize.x / tileWidth;
    const int tilesY = texSize.y / tileHeight;

    m_tileRects.clear();
    for (int ty = 0; ty < tilesY; ++ty) {
        for (int tx = 0; tx < tilesX; ++tx) {
            m_tileRects.push_back(sf::IntRect({ {tx * tileWidth, ty * tileHeight}, {tileWidth, tileHeight} }));
        }
    }

    std::cout << "[World] Loaded tileset: " << path << " (" << m_tileRects.size() << " tiles)\n";
    return true;
}

void World::render(sf::RenderWindow& window, const Camera& cam) const {
    if (m_tiles.empty()) return;

    // Get window size for culling
    const sf::Vector2u winSize = window.getSize();
    const float winW = static_cast<float>(winSize.x);
    const float winH = static_cast<float>(winSize.y);

    // Rough culling bounds (expand a bit to avoid edge artifacts)
    const sf::Vector2i minTile = screenToWorld(-100.f, -100.f, cam);
    const sf::Vector2i maxTile = screenToWorld(winW + 100.f, winH + 100.f, cam);

    // Isometric rendering order: back-to-front (y then x)
    for (int y = std::max(0, minTile.y); y < std::min(m_height, maxTile.y + 1); ++y) {
        for (int x = std::max(0, minTile.x); x < std::min(m_width, maxTile.x + 1); ++x) {
            const Tile& tile = m_tiles[index(x, y)];

            // Skip empty tiles
            if (tile.tileId >= m_tileRects.size()) continue;

            const sf::Vector2f screenPos = worldToScreen(x, y, cam);

            // Skip if totally off-screen
            if (screenPos.x < -200.f || screenPos.x > winW + 200.f ||
                screenPos.y < -200.f || screenPos.y > winH + 200.f) {
                continue;
            }

            // Draw tile
            sf::Sprite sprite(m_tileset, m_tileRects[tile.tileId]);
            sprite.setPosition(screenPos);
            sprite.setScale({ cam.zoom, cam.zoom });

            // Optional: tint non-walkable tiles
            if (!(tile.flags & Tile::Walkable)) {
                sprite.setColor(sf::Color(180, 180, 180));
            }

            window.draw(sprite);
        }
    }
}