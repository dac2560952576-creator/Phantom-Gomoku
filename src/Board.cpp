#include "Board.hpp"

#include <algorithm>
#include <cmath>
#include <random>

Board::Board() {
    reset(Mode::Classic);
    generateTextures();
}

void Board::reset(Mode mode) {
    mode_ = mode;
    temperanceActive_ = false;
    strengthPos_.reset();
    for (auto& row : cells_) {
        row.fill(Cell::Empty);
    }
    moveHistory_.clear();
    lastMove_.reset();
    obstacleOrder_.clear();
}

void Board::draw(sf::RenderTarget& target) const {
    const float boardW = cellSize_ * static_cast<float>(kBoardSize);
    const float boardH = cellSize_ * static_cast<float>(kBoardSize);
    const float boardX = origin_.x - cellSize_ * 0.5f;
    const float boardY = origin_.y - cellSize_ * 0.5f;

    constexpr float thickness = 5.0f;
    const sf::Color boardSideColor(48, 30, 16);

    // Board side — bottom thickness
    {
        sf::RectangleShape bottomSide;
        bottomSide.setPosition({boardX, boardY + boardH});
        bottomSide.setSize({boardW, thickness});
        bottomSide.setFillColor(boardSideColor);
        target.draw(bottomSide);
    }

    // Board side — right thickness
    {
        sf::RectangleShape rightSide;
        rightSide.setPosition({boardX + boardW, boardY});
        rightSide.setSize({thickness, boardH});
        rightSide.setFillColor(boardSideColor);
        target.draw(rightSide);
    }

    // Board top surface
    sf::Sprite boardBg(woodTex_);
    boardBg.setPosition({boardX, boardY});
    boardBg.setTextureRect(sf::IntRect({0, 0}, {static_cast<int>(boardW), static_cast<int>(boardH)}));
    target.draw(boardBg);

    // Inner shadow — subtle dark line along top/left inside edge
    {
        sf::RectangleShape innerTop;
        innerTop.setPosition({boardX, boardY});
        innerTop.setSize({boardW, 2.0f});
        innerTop.setFillColor(sf::Color(30, 18, 8, 90));
        target.draw(innerTop);

        sf::RectangleShape innerLeft;
        innerLeft.setPosition({boardX, boardY});
        innerLeft.setSize({2.0f, boardH});
        innerLeft.setFillColor(sf::Color(30, 18, 8, 90));
        target.draw(innerLeft);
    }

    // Embossed grid lines — bright shadow + dark line for engraved look
    const sf::Color gridColor(60, 38, 18);
    const sf::Color gridHighlight(112, 84, 52);
    for (int i = 0; i < kBoardSize; ++i) {
        const float offset = static_cast<float>(i) * cellSize_;

        // Bright highlight (bottom-right of each dark line)
        {
            sf::Vertex hHL[] = {
                {{origin_.x + 1.0f, origin_.y + offset + 1.0f}, gridHighlight},
                {{origin_.x + cellSize_ * static_cast<float>(kBoardSize - 1) + 1.0f,
                  origin_.y + offset + 1.0f}, gridHighlight}
            };
            sf::Vertex vHL[] = {
                {{origin_.x + offset + 1.0f, origin_.y + 1.0f}, gridHighlight},
                {{origin_.x + offset + 1.0f,
                  origin_.y + cellSize_ * static_cast<float>(kBoardSize - 1) + 1.0f}, gridHighlight}
            };
            target.draw(hHL, 2, sf::PrimitiveType::Lines);
            target.draw(vHL, 2, sf::PrimitiveType::Lines);
        }

        // Dark line on top
        sf::Vertex horizontal[] = {
            {{origin_.x, origin_.y + offset}, gridColor},
            {{origin_.x + cellSize_ * static_cast<float>(kBoardSize - 1), origin_.y + offset}, gridColor}
        };
        sf::Vertex vertical[] = {
            {{origin_.x + offset, origin_.y}, gridColor},
            {{origin_.x + offset, origin_.y + cellSize_ * static_cast<float>(kBoardSize - 1)}, gridColor}
        };
        target.draw(horizontal, 2, sf::PrimitiveType::Lines);
        target.draw(vertical, 2, sf::PrimitiveType::Lines);
    }

    // Star points (hoshi) — 9 standard positions on 15x15
    {
        constexpr int hoshiPositions[][2] = {
            {3, 3}, {3, 7}, {3, 11},
            {7, 3}, {7, 7}, {7, 11},
            {11, 3}, {11, 7}, {11, 11}
        };
        const float dotRadius = cellSize_ * 0.115f;
        for (const auto& [row, col] : hoshiPositions) {
            sf::CircleShape dot(dotRadius);
            dot.setFillColor(gridColor);
            dot.setOrigin({dotRadius, dotRadius});
            dot.setPosition({origin_.x + static_cast<float>(col) * cellSize_,
                             origin_.y + static_cast<float>(row) * cellSize_});
            target.draw(dot);
        }
    }

    if (lastMove_.has_value()) {
        sf::RectangleShape marker;
        marker.setSize({cellSize_ - 6.0f, cellSize_ - 6.0f});
        marker.setFillColor(sf::Color::Transparent);
        marker.setOutlineThickness(2.0f);
        marker.setOutlineColor(sf::Color(200, 30, 30));
        marker.setPosition({
            origin_.x + static_cast<float>(lastMove_->y) * cellSize_ - cellSize_ * 0.5f + 3.0f,
            origin_.y + static_cast<float>(lastMove_->x) * cellSize_ - cellSize_ * 0.5f + 3.0f
        });
        target.draw(marker);
    }

    const float stoneTexHalf = static_cast<float>(blackStoneTex_.getSize().x) * 0.5f;
    const float obsTexHalf = static_cast<float>(obstacleTex_.getSize().x) * 0.5f;

    for (int row = 0; row < kBoardSize; ++row) {
        for (int col = 0; col < kBoardSize; ++col) {
            if (cells_[row][col] == Cell::Empty) {
                continue;
            }

            const float cx = origin_.x + static_cast<float>(col) * cellSize_;
            const float cy = origin_.y + static_cast<float>(row) * cellSize_;

            if (cells_[row][col] == Cell::Obstacle) {
                // Floating animation — gentle vertical bob (each crystal at different phase)
                const float floatTime = obstacleAnimClock_.getElapsedTime().asSeconds();
                const float floatOffset = std::sin(floatTime * 1.6f + static_cast<float>(row + col) * 0.7f) * 2.5f;

                // Placement pop-in animation
                float placeScale = 1.0f;
                const bool hasRecent = obstaclePlaceClock_.getElapsedTime().asSeconds() < kObstaclePlaceDuration;
                if (hasRecent) {
                    for (const auto& rp : recentObstacles_) {
                        if (rp.x == row && rp.y == col) {
                            const float t = obstaclePlaceClock_.getElapsedTime().asSeconds() / kObstaclePlaceDuration;
                            if (t < 0.3f) {
                                placeScale = (t / 0.3f) * 1.25f;
                            } else {
                                placeScale = 1.25f + (1.0f - 1.25f) * ((t - 0.3f) / 0.7f);
                            }
                            break;
                        }
                    }
                }

                // Drop shadow — stays on the board, doesn't float
                const float shadowAlpha = std::max(0.0f, 1.0f - floatOffset / 5.0f);
                const float obShadR = obsTexHalf * 0.55f;
                const float shadowScale = 1.0f - floatOffset * 0.04f;
                sf::CircleShape shadow(obShadR, 8);
                shadow.setPosition({cx - obShadR * shadowScale,
                                    cy - obShadR * 0.7f * shadowScale});
                shadow.setScale({shadowScale, shadowScale * 0.55f});
                shadow.setFillColor(sf::Color(8, 4, 16, static_cast<std::uint8_t>(90.0f * shadowAlpha)));
                target.draw(shadow);

                // Crystal sprite with float offset
                sf::Sprite obsSpr(obstacleTex_);
                obsSpr.setPosition({cx - obsTexHalf * placeScale + floatOffset * 0.5f,
                                    cy - obsTexHalf * placeScale + floatOffset});
                obsSpr.setScale({placeScale, placeScale});
                target.draw(obsSpr);
                continue;
            }

            // Drop shadow — stone sitting on the board
            {
                const float sr = stoneRadius_ * 2.0f * 0.85f;
                sf::CircleShape shadow(sr, 10);
                shadow.setPosition({cx - sr, cy - sr * 0.35f + 2.0f});
                shadow.setScale({1.0f, 0.42f});
                shadow.setFillColor(sf::Color(6, 4, 14, 55));
                target.draw(shadow);
            }

            sf::Sprite stoneSpr(cells_[row][col] == Cell::Black ? blackStoneTex_ : whiteStoneTex_);
            stoneSpr.setScale({2.0f, 2.0f});
            stoneSpr.setPosition({cx - stoneTexHalf * 2.0f, cy - stoneTexHalf * 2.0f});

            // Scale-pop animation for last placed stone
            if (lastMove_.has_value() && row == lastMove_->x && col == lastMove_->y) {
                const float elapsed = lastMoveClock_.getElapsedTime().asSeconds();
                if (elapsed < kPlaceAnimDuration) {
                    const float t = elapsed / kPlaceAnimDuration;
                    float scale;
                    if (t < 0.35f) {
                        scale = (t / 0.35f) * 1.15f;
                    } else {
                        scale = 1.15f + (1.0f - 1.15f) * ((t - 0.35f) / 0.65f);
                    }
                    stoneSpr.setScale({scale * 2.0f, scale * 2.0f});
                    stoneSpr.setPosition({cx - stoneTexHalf * 2.0f * scale, cy - stoneTexHalf * 2.0f * scale});
                }
            }

            target.draw(stoneSpr);
        }
    }
}

void Board::generateTextures() {
    // --- Wood grain board texture (tileable 64x64) ---
    {
        constexpr int ts = 64;
        sf::Image img({ts, ts});
        for (int y = 0; y < ts; ++y) {
            const int grain = (y / 6) % 3;
            for (int x = 0; x < ts; ++x) {
                const int noise = ((x * 7 + y * 13) % 11) - 5;
                const int r = 192 + grain * 4 + noise;
                const int g = 148 + grain * 3 + noise / 2;
                const int b = 104 + grain * 2 + noise / 3;
                img.setPixel({static_cast<unsigned>(x), static_cast<unsigned>(y)},
                             sf::Color(static_cast<std::uint8_t>(std::clamp(r, 0, 255)),
                                       static_cast<std::uint8_t>(std::clamp(g, 0, 255)),
                                       static_cast<std::uint8_t>(std::clamp(b, 0, 255))));
            }
        }
        static_cast<void>(woodTex_.loadFromImage(img));
        woodTex_.setSmooth(false);
        woodTex_.setRepeated(true);
    }

    // --- Stone textures (16x16 pixel-art with 2.5D shading) ---
    // Light from top-left. Dithered gradient for smooth 3D sphere illusion.
    {
        constexpr int ss = 16;
        const float cx = (ss - 1) * 0.5f;
        const float cy = (ss - 1) * 0.5f;
        const float radius = 7.0f;

        // Black stone — mostly dark, small bright highlight
        {
            const sf::Color pal[] = {
                {140, 139, 144},  // 0: tiny specular
                {80, 79, 84},     // 1: specular fade
                {48, 47, 52},     // 2: highlight edge
                {28, 27, 32},     // 3: mid-light
                {15, 15, 18},     // 4: mid-dark
                {7, 7, 10},       // 5: shadow
                {3, 3, 5},        // 6: deep
                {1, 1, 3},        // 7: near-black rim
            };

            sf::Image img({ss, ss}, sf::Color::Transparent);
            for (int py = 0; py < ss; ++py) {
                for (int px = 0; px < ss; ++px) {
                    const float dx = static_cast<float>(px) - cx;
                    const float dy = static_cast<float>(py) - cy;
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    if (dist > radius) continue;

                    // Directional light — heavily biased to dark side
                    const float dirLight = 1.0f - (dx + dy) * 0.38f / radius;
                    const float edge = dist / radius;
                    const float edgeDark = edge * edge * 0.7f;
                    // Push light down so only top-left ~25% is bright
                    const float combined = std::clamp(dirLight * 1.4f - edgeDark - 0.25f, 0.0f, 1.0f);

                    const int idx = std::clamp(static_cast<int>((1.0f - combined) * 7.5f), 0, 7);
                    img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)}, pal[idx]);
                }
            }
            // Single sharp specular pixel at top-left
            img.setPixel({4, 3}, {200, 198, 205});

            static_cast<void>(blackStoneTex_.loadFromImage(img));
            blackStoneTex_.setSmooth(false);
        }

        // White stone
        {
            const sf::Color pal[] = {
                {252, 250, 245},  // 0: specular bright
                {235, 228, 215},  // 1: specular edge
                {210, 200, 185},  // 2: highlight
                {180, 170, 155},  // 3: mid-light
                {150, 140, 126},  // 4: mid-dark
                {122, 112, 100},  // 5: shadow
                {95, 85, 75},     // 6: deep shadow
                {70, 62, 54},     // 7: rim
            };

            sf::Image img({ss, ss}, sf::Color::Transparent);
            for (int py = 0; py < ss; ++py) {
                for (int px = 0; px < ss; ++px) {
                    const float dx = static_cast<float>(px) - cx;
                    const float dy = static_cast<float>(py) - cy;
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    if (dist > radius) continue;

                    const float dirLight = 1.0f - (dx + dy) * 0.35f / radius;
                    const float edge = dist / radius;
                    const float edgeDark = edge * edge * 0.5f;
                    const float combined = std::clamp(dirLight - edgeDark, 0.0f, 1.0f);

                    const int idx = std::clamp(static_cast<int>((1.0f - combined) * 7.5f), 0, 7);
                    img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)}, pal[idx]);
                }
            }
            // Sharp specular dot
            img.setPixel({4, 3}, {255, 254, 252});
            img.setPixel({5, 3}, {252, 250, 247});
            img.setPixel({4, 4}, {254, 252, 249});

            static_cast<void>(whiteStoneTex_.loadFromImage(img));
            whiteStoneTex_.setSmooth(false);
        }
    }

    // --- Obstacle texture (32x32 pixel-art 2.5D dark crystal prism) ---
    {
        constexpr int os = 32;
        sf::Image img({os, os}, sf::Color::Transparent);

        // Draw a 2.5D crystal prism with clear top/side faces
        // Top face vertices (isometric-ish diamond)
        const int cx = os / 2;
        const int topY = 3;
        const int midY = 16;
        const int botY = 27;

        // Top diamond: left(-6,12), top(cx,topY), right(+6,12), bottom(cx,midY)
        // Left side quad
        // Right side quad
        // Bottom tip

        for (int y = 0; y < os; ++y) {
            for (int x = 0; x < os; ++x) {
                const float dx = static_cast<float>(x - cx);

                // Determine which face this pixel belongs to
                bool isTopFace = false;
                bool isLeftFace = false;
                bool isRightFace = false;
                bool isBottomTip = false;

                // Top face: diamond shape from topY to midY
                if (y >= topY && y <= midY) {
                    const float t = static_cast<float>(y - topY) / static_cast<float>(midY - topY);
                    const float halfW = 6.0f * (1.0f - t) + 10.0f * t;
                    const float topDX = std::abs(dx);
                    if (topDX <= halfW) {
                        // Check if closer to top edge (top face) or side
                        const float topEdgeY = topY + (1.0f - topDX / 10.0f) * static_cast<float>(midY - topY);
                        if (y <= topEdgeY + 1.0f) {
                            isTopFace = true;
                        } else if (dx <= 0) {
                            isLeftFace = true;
                        } else {
                            isRightFace = true;
                        }
                    }
                } else if (y > midY && y <= botY) {
                    const float t = static_cast<float>(y - midY) / static_cast<float>(botY - midY);
                    const float halfW = 10.0f * (1.0f - t) + 3.0f * t;
                    const float topDX = std::abs(dx);
                    if (topDX <= halfW) {
                        if (dx <= 0) {
                            isLeftFace = true;
                        } else {
                            isRightFace = true;
                        }
                    }
                } else if (y > botY && y <= botY + 2) {
                    const float halfW = 3.0f * (1.0f - static_cast<float>(y - botY) / 2.0f);
                    if (std::abs(dx) <= halfW) {
                        isBottomTip = true;
                    }
                }

                if (!isTopFace && !isLeftFace && !isRightFace && !isBottomTip) {
                    continue;
                }

                int r, g, b;

                if (isTopFace) {
                    // Top face — brightest, catching overhead light
                    const float edgeFade = std::abs(dx) / 10.0f;
                    r = 110 - static_cast<int>(edgeFade * 25.0f);
                    g = 65 - static_cast<int>(edgeFade * 20.0f);
                    b = 130 - static_cast<int>(edgeFade * 25.0f);
                    // Top highlight streak
                    if (std::abs(dx) < 2.0f && y < topY + 4) {
                        r = std::min(255, r + 35);
                        g = std::min(255, g + 25);
                        b = std::min(255, b + 40);
                    }
                } else if (isLeftFace) {
                    // Left face — medium shade
                    const float yNorm = static_cast<float>(y - midY) / static_cast<float>(botY - midY);
                    r = 75 - static_cast<int>(yNorm * 20.0f);
                    g = 38 - static_cast<int>(yNorm * 15.0f);
                    b = 100 - static_cast<int>(yNorm * 25.0f);
                } else if (isRightFace) {
                    // Right face — darkest (away from light)
                    const float yNorm = static_cast<float>(y - midY) / static_cast<float>(botY - midY);
                    r = 50 - static_cast<int>(yNorm * 18.0f);
                    g = 22 - static_cast<int>(yNorm * 12.0f);
                    b = 72 - static_cast<int>(yNorm * 20.0f);
                } else {
                    // Bottom tip
                    r = 30; g = 12; b = 48;
                }

                // Subtle pixel noise
                const int noise = ((x * 13 + y * 7) % 5) - 2;
                r = std::clamp(r + noise, 0, 255);
                g = std::clamp(g + noise, 0, 255);
                b = std::clamp(b + noise, 0, 255);

                if (!isTopFace && !isLeftFace && !isRightFace && !isBottomTip) continue;

                img.setPixel({static_cast<unsigned>(x), static_cast<unsigned>(y)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b)));
            }
        }

        static_cast<void>(obstacleTex_.loadFromImage(img));
        obstacleTex_.setSmooth(false);
    }
}

bool Board::loadBoardTexture(const std::string& path) {
    if (!woodTex_.loadFromFile(path)) {
        return false;
    }
    woodTex_.setSmooth(false);
    woodTex_.setRepeated(true);
    return true;
}

bool Board::placePieceFromPixel(sf::Vector2i pixel, Piece piece) {
    const auto cell = pixelToCell(pixel);
    if (!cell.has_value()) {
        return false;
    }
    return placePiece(cell->x, cell->y, piece);
}

bool Board::placePieceAt(int row, int col, Piece piece) {
    return placePiece(row, col, piece);
}

std::optional<Board::Move> Board::undoLastMove() {
    if (moveHistory_.empty()) {
        return std::nullopt;
    }

    const Move last = moveHistory_.back();
    moveHistory_.pop_back();

    if (isInside(last.row, last.col)) {
        cells_[last.row][last.col] = Cell::Empty;
    }

    if (moveHistory_.empty()) {
        lastMove_.reset();
    } else {
        lastMove_ = sf::Vector2i(moveHistory_.back().row, moveHistory_.back().col);
    }

    return last;
}

bool Board::canPlaceAt(int row, int col) const {
    return isInside(row, col) && cells_[row][col] == Cell::Empty;
}

bool Board::hasEmptyCell() const {
    for (const auto& row : cells_) {
        for (const auto cell : row) {
            if (cell == Cell::Empty) {
                return true;
            }
        }
    }
    return false;
}

Board::Piece Board::pieceAt(int row, int col) const {
    if (!isInside(row, col)) {
        return Piece::None;
    }
    if (cells_[row][col] == Cell::Black) {
        return Piece::Black;
    }
    if (cells_[row][col] == Cell::White) {
        return Piece::White;
    }
    return Piece::None;
}

Board::Piece Board::winnerFromLastMove() const {
    if (!lastMove_.has_value()) {
        return Piece::None;
    }

    const int row = lastMove_->x;
    const int col = lastMove_->y;
    const Piece piece = pieceAt(row, col);
    if (piece == Piece::None) {
        return Piece::None;
    }

    const int horizontal = 1 + countDirection(row, col, 0, 1, piece) + countDirection(row, col, 0, -1, piece);
    const int vertical = 1 + countDirection(row, col, 1, 0, piece) + countDirection(row, col, -1, 0, piece);
    const int diagonalDown = 1 + countDirection(row, col, 1, 1, piece) + countDirection(row, col, -1, -1, piece);
    const int diagonalUp = 1 + countDirection(row, col, 1, -1, piece) + countDirection(row, col, -1, 1, piece);

    if (horizontal >= 5 || vertical >= 5 || diagonalDown >= 5 || diagonalUp >= 5) {
        return piece;
    }
    return Piece::None;
}

Board::Mode Board::mode() const {
    return mode_;
}

std::optional<sf::Vector2i> Board::lastMovePosition() const {
    if (lastMove_.has_value()) {
        return lastMove_;
    }
    return std::nullopt;
}

bool Board::isInside(int row, int col) const {
    return row >= 0 && row < kBoardSize && col >= 0 && col < kBoardSize;
}

bool Board::placePiece(int row, int col, Piece piece) {
    if (!isInside(row, col) || cells_[row][col] != Cell::Empty) {
        return false;
    }

    cells_[row][col] = piece == Piece::Black ? Cell::Black : Cell::White;
    moveHistory_.push_back({row, col, piece});
    lastMove_ = sf::Vector2i(row, col);
    lastMoveClock_.restart();
    return true;
}

std::optional<sf::Vector2i> Board::pixelToCell(sf::Vector2i pixel) const {
    const float localX = static_cast<float>(pixel.x) - origin_.x;
    const float localY = static_cast<float>(pixel.y) - origin_.y;

    const int col = static_cast<int>(std::lround(localX / cellSize_));
    const int row = static_cast<int>(std::lround(localY / cellSize_));

    if (!isInside(row, col)) {
        return std::nullopt;
    }

    const float centerX = origin_.x + static_cast<float>(col) * cellSize_;
    const float centerY = origin_.y + static_cast<float>(row) * cellSize_;
    const float maxDistance = cellSize_ * 0.5f;

    if (std::abs(static_cast<float>(pixel.x) - centerX) > maxDistance ||
        std::abs(static_cast<float>(pixel.y) - centerY) > maxDistance) {
        return std::nullopt;
    }

    return sf::Vector2i(row, col);
}

sf::Vector2f Board::cellToPixel(int row, int col) const {
    return {origin_.x + static_cast<float>(col) * cellSize_,
            origin_.y + static_cast<float>(row) * cellSize_};
}

int Board::countDirection(int row, int col, int dRow, int dCol, Piece piece) const {
    int count = 0;
    int currentRow = row + dRow;
    int currentCol = col + dCol;

    while (isInside(currentRow, currentCol)) {
        const auto p = pieceAt(currentRow, currentCol);
        if (p == piece) {
            ++count;
            // Strength: empowered cell counts as 2
            if (strengthPos_.has_value() &&
                currentRow == strengthPos_->x && currentCol == strengthPos_->y) {
                ++count;
            }
        } else if (temperanceActive_ && cells_[currentRow][currentCol] == Cell::Obstacle) {
            // Temperance: skip obstacle, continue counting
            currentRow += dRow;
            currentCol += dCol;
            continue;
        } else {
            break;
        }
        currentRow += dRow;
        currentCol += dCol;
    }

    return count;
}

void Board::setTemperanceActive(bool active) {
    temperanceActive_ = active;
}

bool Board::isTemperanceActive() const {
    return temperanceActive_;
}

void Board::setStrengthPos(std::optional<sf::Vector2i> pos) {
    strengthPos_ = pos;
}

std::optional<sf::Vector2i> Board::strengthPos() const {
    return strengthPos_;
}

void Board::generateRandomObstacles(int count) {
    clearObstacles();

    std::vector<sf::Vector2i> emptyCells;
    for (int row = 0; row < kBoardSize; ++row) {
        for (int col = 0; col < kBoardSize; ++col) {
            if (cells_[row][col] == Cell::Empty) {
                emptyCells.push_back({row, col});
            }
        }
    }

    static thread_local std::mt19937 rng(std::random_device{}());
    const int actualCount = std::min(count, static_cast<int>(emptyCells.size()));
    for (int i = 0; i < actualCount; ++i) {
        const std::size_t idx = std::uniform_int_distribution<std::size_t>(0, emptyCells.size() - 1)(rng);
        const auto pos = emptyCells[idx];
        cells_[pos.x][pos.y] = Cell::Obstacle;
        obstacleOrder_.push_back(pos);
        emptyCells.erase(emptyCells.begin() + static_cast<std::ptrdiff_t>(idx));
    }
}

void Board::spawnRandomObstacles(int count) {
    std::vector<sf::Vector2i> emptyCells;
    for (int row = 0; row < kBoardSize; ++row) {
        for (int col = 0; col < kBoardSize; ++col) {
            if (cells_[row][col] == Cell::Empty) {
                emptyCells.push_back({row, col});
            }
        }
    }

    static thread_local std::mt19937 rng(std::random_device{}());
    const int available = std::min({count, kMaxObstacles - obstacleCount(), static_cast<int>(emptyCells.size())});
    std::vector<sf::Vector2i> newPositions;
    for (int i = 0; i < available; ++i) {
        const std::size_t idx = std::uniform_int_distribution<std::size_t>(0, emptyCells.size() - 1)(rng);
        const auto pos = emptyCells[idx];
        cells_[pos.x][pos.y] = Cell::Obstacle;
        obstacleOrder_.push_back(pos);
        newPositions.push_back(pos);
        emptyCells.erase(emptyCells.begin() + static_cast<std::ptrdiff_t>(idx));
    }
    if (!newPositions.empty()) {
        recordObstaclePlacements(newPositions);
    }
}

void Board::removeOldestObstacles(int count) {
    const int actual = std::min(count, static_cast<int>(obstacleOrder_.size()));
    for (int i = 0; i < actual; ++i) {
        const auto pos = obstacleOrder_.front();
        obstacleOrder_.pop_front();
        if (isInside(pos.x, pos.y) && cells_[pos.x][pos.y] == Cell::Obstacle) {
            cells_[pos.x][pos.y] = Cell::Empty;
        }
    }
}

void Board::clearObstacles() {
    for (auto& row : cells_) {
        for (auto& cell : row) {
            if (cell == Cell::Obstacle) {
                cell = Cell::Empty;
            }
        }
    }
    obstacleOrder_.clear();
}

std::vector<sf::Vector2i> Board::obstaclePositions() const {
    std::vector<sf::Vector2i> positions;
    for (int row = 0; row < kBoardSize; ++row) {
        for (int col = 0; col < kBoardSize; ++col) {
            if (cells_[row][col] == Cell::Obstacle) {
                positions.push_back({row, col});
            }
        }
    }
    return positions;
}

void Board::setObstacles(const std::vector<sf::Vector2i>& positions) {
    clearObstacles();
    for (const auto& pos : positions) {
        if (isInside(pos.x, pos.y) && cells_[pos.x][pos.y] == Cell::Empty) {
            cells_[pos.x][pos.y] = Cell::Obstacle;
            obstacleOrder_.push_back(pos);
        }
    }
}

void Board::recordObstaclePlacements(const std::vector<sf::Vector2i>& positions) {
    recentObstacles_ = positions;
    obstaclePlaceClock_.restart();
}

void Board::clearExpiredObstacleAnimations() {
    if (!recentObstacles_.empty() &&
        obstaclePlaceClock_.getElapsedTime().asSeconds() >= kObstaclePlaceDuration) {
        recentObstacles_.clear();
    }
}

int Board::obstacleCount() const {
    int count = 0;
    for (const auto& row : cells_) {
        for (const auto cell : row) {
            if (cell == Cell::Obstacle) {
                ++count;
            }
        }
    }
    return count;
}

int Board::totalPieceCount() const {
    int count = 0;
    for (const auto& row : cells_) {
        for (const auto cell : row) {
            if (cell == Cell::Black || cell == Cell::White) {
                ++count;
            }
        }
    }
    return count;
}

void Board::spawnSmartObstacles(int count) {
    struct WeightedCell {
        sf::Vector2i pos;
        int weight;
    };

    std::vector<WeightedCell> candidates;
    for (int row = 0; row < kBoardSize; ++row) {
        for (int col = 0; col < kBoardSize; ++col) {
            if (cells_[row][col] != Cell::Empty) {
                continue;
            }

            int nearbyPieces = 0;
            for (int dr = -2; dr <= 2; ++dr) {
                for (int dc = -2; dc <= 2; ++dc) {
                    if (dr == 0 && dc == 0) {
                        continue;
                    }
                    const int nr = row + dr;
                    const int nc = col + dc;
                    if (isInside(nr, nc) &&
                        (cells_[nr][nc] == Cell::Black || cells_[nr][nc] == Cell::White)) {
                        ++nearbyPieces;
                    }
                }
            }

            const int centerBias = 14 - (std::abs(row - 7) + std::abs(col - 7));
            const int weight = nearbyPieces * 100 + centerBias * 5 + 1;
            candidates.push_back({{row, col}, weight});
        }
    }

    if (candidates.empty()) {
        return;
    }

    static thread_local std::mt19937 rng(std::random_device{}());
    const int available = std::min({count, kMaxObstacles - obstacleCount(),
                                    static_cast<int>(candidates.size())});

    for (int i = 0; i < available; ++i) {
        int totalWeight = 0;
        for (const auto& c : candidates) {
            totalWeight += c.weight;
        }

        if (totalWeight == 0) {
            break;
        }

        int roll = std::uniform_int_distribution<>(0, totalWeight - 1)(rng);
        std::size_t chosen = 0;
        int cumulative = 0;
        for (std::size_t j = 0; j < candidates.size(); ++j) {
            cumulative += candidates[j].weight;
            if (roll < cumulative) {
                chosen = j;
                break;
            }
        }

        const auto pos = candidates[chosen].pos;
        cells_[pos.x][pos.y] = Cell::Obstacle;
        obstacleOrder_.push_back(pos);
        candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(chosen));
    }

    // Record for placement animation
    if (available > 0) {
        std::vector<sf::Vector2i> newPositions;
        const auto& order = obstacleOrder_;
        auto it = order.rbegin();
        for (int i = 0; i < available && it != order.rend(); ++i, ++it) {
            newPositions.push_back(*it);
        }
        recordObstaclePlacements(newPositions);
    }
}

int Board::pieceCount(Piece piece) const {
    const Cell target = piece == Piece::Black ? Cell::Black : Cell::White;
    int count = 0;
    for (const auto& row : cells_) {
        for (const auto cell : row) {
            if (cell == target) ++count;
        }
    }
    return count;
}

void Board::removePieceAt(int row, int col) {
    if (isInside(row, col) && (cells_[row][col] == Cell::Black || cells_[row][col] == Cell::White)) {
        cells_[row][col] = Cell::Empty;
    }
}

void Board::removeObstacleAt(int row, int col) {
    if (isInside(row, col) && cells_[row][col] == Cell::Obstacle) {
        cells_[row][col] = Cell::Empty;
        // Remove from obstacle order
        for (auto it = obstacleOrder_.begin(); it != obstacleOrder_.end(); ++it) {
            if (it->x == row && it->y == col) {
                obstacleOrder_.erase(it);
                break;
            }
        }
    }
}

void Board::placeObstacleAt(int row, int col) {
    if (isInside(row, col) && cells_[row][col] == Cell::Empty && obstacleCount() < kMaxObstacles) {
        cells_[row][col] = Cell::Obstacle;
        obstacleOrder_.push_back({row, col});
        recordObstaclePlacements({{row, col}});
    }
}

void Board::clearArea(int centerRow, int centerCol, int radius) {
    for (int r = centerRow - radius; r <= centerRow + radius; ++r) {
        for (int c = centerCol - radius; c <= centerCol + radius; ++c) {
            if (isInside(r, c)) {
                if (cells_[r][c] == Cell::Black || cells_[r][c] == Cell::White) {
                    cells_[r][c] = Cell::Empty;
                }
            }
        }
    }
}

void Board::swapPieces(int r1, int c1, int r2, int c2) {
    if (!isInside(r1, c1) || !isInside(r2, c2)) return;
    const auto a = cells_[r1][c1];
    const auto b = cells_[r2][c2];
    if (a != Cell::Black && a != Cell::White) return;
    if (b != Cell::Black && b != Cell::White) return;
    if (a == b) return;
    cells_[r1][c1] = b;
    cells_[r2][c2] = a;
}

std::vector<sf::Vector2i> Board::findLongestChainCells(Piece piece) const {
    const Cell target = piece == Piece::Black ? Cell::Black : Cell::White;
    std::vector<sf::Vector2i> longest;
    int maxLen = 0;

    // Directions: right, down, down-right, down-left
    constexpr int dirs[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};

    for (int row = 0; row < kBoardSize; ++row) {
        for (int col = 0; col < kBoardSize; ++col) {
            if (cells_[row][col] != target) continue;
            for (const auto& d : dirs) {
                // Only count from chain start (no same piece in reverse direction)
                const int pr = row - d[0];
                const int pc = col - d[1];
                if (isInside(pr, pc) && cells_[pr][pc] == target) continue;

                std::vector<sf::Vector2i> chain;
                int r = row, c = col;
                while (isInside(r, c) && cells_[r][c] == target) {
                    chain.push_back({r, c});
                    r += d[0];
                    c += d[1];
                }
                if (static_cast<int>(chain.size()) > maxLen) {
                    maxLen = static_cast<int>(chain.size());
                    longest = std::move(chain);
                }
            }
        }
    }
    return longest;
}

std::vector<sf::Vector2i> Board::findAdjacentEmptyCells(Piece piece) const {
    const Cell target = piece == Piece::Black ? Cell::Black : Cell::White;
    std::vector<sf::Vector2i> result;
    for (int row = 0; row < kBoardSize; ++row) {
        for (int col = 0; col < kBoardSize; ++col) {
            if (cells_[row][col] != target) continue;
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    if (dr == 0 && dc == 0) continue;
                    const int nr = row + dr;
                    const int nc = col + dc;
                    if (isInside(nr, nc) && cells_[nr][nc] == Cell::Empty) {
                        // Check if already in result
                        bool found = false;
                        for (const auto& p : result) {
                            if (p.x == nr && p.y == nc) { found = true; break; }
                        }
                        if (!found) result.push_back({nr, nc});
                    }
                }
            }
        }
    }
    return result;
}

