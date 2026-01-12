#pragma once
#include <SFML/Graphics.hpp>

// Generate a simple colored placeholder tileset for testing
inline sf::Image generatePlaceholderTileset() {
    const int tileW = 64;
    const int tileH = 32;
    const int tilesPerRow = 4;
    const int rows = 4;

    sf::Image img({ tileW * tilesPerRow, tileH * rows }, sf::Color(0, 0, 0, 0));

    // Helper to draw an isometric diamond
    auto drawIsoDiamond = [&](int tx, int ty, sf::Color color) {
        const int ox = tx * tileW + tileW / 2;
        const int oy = ty * tileH;

        // Simple diamond shape
        for (int y = 0; y < tileH; ++y) {
            float ratio = (y < tileH / 2) ? (float)y / (tileH / 2) : (float)(tileH - y) / (tileH / 2);
            int width = static_cast<int>(ratio * (tileW / 2));

            for (int dx = -width; dx <= width; ++dx) {
                int px = ox + dx;
                int py = oy + y;
                if (px >= tx * tileW && px < (tx + 1) * tileW && py >= ty * tileH && py < (ty + 1) * tileH) {
                    img.setPixel({ (unsigned)px,(unsigned)py }, color);
                }
            }
        }
        };

    // Tile 0: Grass floor
    drawIsoDiamond(0, 0, sf::Color(80, 140, 60));

    // Tile 1: Stone floor
    drawIsoDiamond(1, 0, sf::Color(120, 120, 130));

    // Tile 2: Dirt floor
    drawIsoDiamond(2, 0, sf::Color(140, 100, 60));

    // Tile 3: Sand floor
    drawIsoDiamond(3, 0, sf::Color(200, 180, 100));

    // Tile 10 (row 2, col 2): Wall
    drawIsoDiamond(2, 2, sf::Color(60, 50, 50));

    return img;
}

inline bool createPlaceholderTilesetFile(const std::string& path) {
    sf::Image img = generatePlaceholderTileset();
    return img.saveToFile(path);
}