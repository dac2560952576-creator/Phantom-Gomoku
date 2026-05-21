#pragma once

#include <SFML/Graphics.hpp>

#include <array>
#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

class Board {
public:
    enum class Piece {
        None,
        Black,
        White
    };

    static constexpr int kBoardSize = 15;
    static constexpr int kMaxObstacles = 8;

    enum class Mode {
        Classic,
        Obstacle
    };

    struct Move {
        int row = 0;
        int col = 0;
        Piece piece = Piece::None;
    };

    Board();

    void reset(Mode mode);
    void draw(sf::RenderTarget& target) const;

    bool placePieceFromPixel(sf::Vector2i pixel, Piece piece);
    bool placePieceAt(int row, int col, Piece piece);
    std::optional<Move> undoLastMove();
    bool canPlaceAt(int row, int col) const;
    bool hasEmptyCell() const;
    Piece pieceAt(int row, int col) const;
    Piece winnerFromLastMove() const;
    Mode mode() const;
    std::optional<sf::Vector2i> lastMovePosition() const;
    sf::Vector2f cellToPixel(int row, int col) const;
    std::optional<sf::Vector2i> pixelToCell(sf::Vector2i pixel) const;

    void generateRandomObstacles(int count);
    void spawnRandomObstacles(int count);
    void spawnSmartObstacles(int count);
    void removeOldestObstacles(int count);
    void clearObstacles();
    int obstacleCount() const;
    std::vector<sf::Vector2i> obstaclePositions() const;
    void setObstacles(const std::vector<sf::Vector2i>& positions);
    void recordObstaclePlacements(const std::vector<sf::Vector2i>& positions);
    void clearExpiredObstacleAnimations();
    int totalPieceCount() const;
    int pieceCount(Piece piece) const;
    bool loadBoardTexture(const std::string& path);

    // Card effect operations
    void removePieceAt(int row, int col);
    void removeObstacleAt(int row, int col);
    void placeObstacleAt(int row, int col);
    void clearArea(int centerRow, int centerCol, int radius);
    void swapPieces(int r1, int c1, int r2, int c2);
    std::vector<sf::Vector2i> findLongestChainCells(Piece piece) const;
    std::vector<sf::Vector2i> findAdjacentEmptyCells(Piece piece) const;

    void setTemperanceActive(bool active);
    bool isTemperanceActive() const;

    void setStrengthPos(std::optional<sf::Vector2i> pos);
    std::optional<sf::Vector2i> strengthPos() const;

private:
    enum class Cell {
        Empty,
        Obstacle,
        Black,
        White
    };

    void generateTextures();

    bool isInside(int row, int col) const;
    bool placePiece(int row, int col, Piece piece);
    int countDirection(int row, int col, int dRow, int dCol, Piece piece) const;

    std::array<std::array<Cell, kBoardSize>, kBoardSize> cells_{};
    std::vector<Move> moveHistory_{};
    std::deque<sf::Vector2i> obstacleOrder_{};
    std::optional<sf::Vector2i> lastMove_;
    sf::Clock lastMoveClock_;
    static constexpr float kPlaceAnimDuration = 0.22f;
    sf::Clock obstacleAnimClock_;
    std::vector<sf::Vector2i> recentObstacles_;
    sf::Clock obstaclePlaceClock_;
    static constexpr float kObstaclePlaceDuration = 0.28f;
    Mode mode_ = Mode::Classic;
    bool temperanceActive_ = false;
    std::optional<sf::Vector2i> strengthPos_;
    sf::Vector2f origin_{130.0f, 195.0f};
    float cellSize_ = 36.0f;
    float stoneRadius_ = 7.0f;

    sf::Texture woodTex_;
    sf::Texture blackStoneTex_;
    sf::Texture whiteStoneTex_;
    sf::Texture obstacleTex_;
};
