#include "Game.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

Game::Game()
    : window_(sf::VideoMode({1280, 820}), "Phantom Gomoku") {
    window_.setFramerateLimit(60);
    fontLoaded_ = loadFont();
    updateWindowTitle();
}

void Game::run() {
    while (window_.isOpen()) {
        processEvents();
        update();
        render();
    }
}

void Game::processEvents() {
    while (const std::optional event = window_.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            window_.close();
            continue;
        }

        if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
            if (keyPressed->scancode == sf::Keyboard::Scancode::Escape) {
                if (scene_ == Scene::Playing) {
                    scene_ = Scene::Menu;
                    updateWindowTitle();
                } else {
                    window_.close();
                }
                continue;
            }

            if (scene_ == Scene::Menu) {
                if (keyPressed->scancode == sf::Keyboard::Scancode::Enter) {
                    startMatch(Board::Mode::Classic, true);
                }
                continue;
            }

            if (keyPressed->scancode == sf::Keyboard::Scancode::R) {
                restart();
            } else if (keyPressed->scancode == sf::Keyboard::Scancode::O) {
                toggleMode();
            } else if (keyPressed->scancode == sf::Keyboard::Scancode::P) {
                toggleAi();
            } else if (keyPressed->scancode == sf::Keyboard::Scancode::U ||
                       keyPressed->scancode == sf::Keyboard::Scancode::Backspace) {
                undoMove();
            } else if (keyPressed->scancode == sf::Keyboard::Scancode::M) {
                scene_ = Scene::Menu;
                updateWindowTitle();
            }
        } else if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mousePressed->button != sf::Mouse::Button::Left) {
                continue;
            }

            const sf::Vector2f mousePosition{
                static_cast<float>(mousePressed->position.x),
                static_cast<float>(mousePressed->position.y)
            };

            if (scene_ == Scene::Menu) {
                handleMenuClick(mousePosition);
            } else {
                handleGameClick(mousePosition);
            }
        }
    }
}

void Game::update() {
}

void Game::render() {
    if (scene_ == Scene::Menu) {
        window_.clear(sf::Color(15, 25, 43));
        drawMenu();
    } else {
        window_.clear(sf::Color(233, 224, 203));
        drawGameScene();
    }

    window_.display();
}

void Game::startMatch(Board::Mode mode, bool aiEnabled) {
    boardMode_ = mode;
    aiEnabled_ = aiEnabled;
    scene_ = Scene::Playing;
    restart();
}

void Game::tryPlacePiece(sf::Vector2i pixel) {
    if (scene_ != Scene::Playing || gameOver_ || (aiEnabled_ && currentTurn_ == Board::Piece::White)) {
        return;
    }

    if (!board_.placePieceFromPixel(pixel, currentTurn_)) {
        return;
    }

    winner_ = board_.winnerFromLastMove();
    if (winner_ != Board::Piece::None) {
        gameOver_ = true;
    } else if (!board_.hasEmptyCell()) {
        gameOver_ = true;
    } else {
        currentTurn_ = nextTurn(currentTurn_);
    }

    maybeMakeAiMove();
    updateWindowTitle();
}

void Game::handleMenuClick(sf::Vector2f mousePosition) {
    const UiButton classicAi = makeButton({110.0f, 286.0f}, {490.0f, 138.0f}, "经典模式 · 人机对战",
                                          sf::Color(198, 150, 73), sf::Color(255, 221, 160));
    const UiButton obstacleAi = makeButton({110.0f, 456.0f}, {490.0f, 138.0f}, "幻格模式 · 人机对战",
                                           sf::Color(93, 122, 170), sf::Color(181, 209, 255));
    const UiButton localTwoPlayer = makeButton({680.0f, 286.0f}, {490.0f, 138.0f}, "经典模式 · 双人对战",
                                               sf::Color(81, 145, 108), sf::Color(181, 237, 197));
    const UiButton exitButton = makeButton({680.0f, 456.0f}, {490.0f, 138.0f}, "退出游戏",
                                           sf::Color(138, 78, 70), sf::Color(237, 166, 156));

    if (classicAi.bounds.contains(mousePosition)) {
        startMatch(Board::Mode::Classic, true);
    } else if (obstacleAi.bounds.contains(mousePosition)) {
        startMatch(Board::Mode::Obstacle, true);
    } else if (localTwoPlayer.bounds.contains(mousePosition)) {
        startMatch(Board::Mode::Classic, false);
    } else if (exitButton.bounds.contains(mousePosition)) {
        window_.close();
    }
}

void Game::handleGameClick(sf::Vector2f mousePosition) {
    const UiButton menuButton = makeButton({790.0f, 382.0f}, {190.0f, 62.0f}, "返回主菜单",
                                           sf::Color(57, 76, 108), sf::Color(161, 193, 255));
    const UiButton restartButton = makeButton({1002.0f, 382.0f}, {190.0f, 62.0f}, "重新开始",
                                              sf::Color(115, 84, 47), sf::Color(236, 203, 144));
    const UiButton undoButton = makeButton({790.0f, 460.0f}, {190.0f, 62.0f}, "悔棋",
                                           sf::Color(71, 111, 82), sf::Color(177, 232, 191));
    const UiButton boardButton = makeButton({1002.0f, 460.0f}, {190.0f, 62.0f}, "切换棋盘",
                                            sf::Color(78, 97, 139), sf::Color(177, 198, 245));
    const UiButton aiButton = makeButton({790.0f, 538.0f}, {402.0f, 62.0f},
                                         aiEnabled_ ? "切换为双人对战" : "切换为人机对战",
                                         sf::Color(111, 73, 122), sf::Color(223, 173, 240));

    if (menuButton.bounds.contains(mousePosition)) {
        scene_ = Scene::Menu;
        updateWindowTitle();
        return;
    }
    if (restartButton.bounds.contains(mousePosition)) {
        restart();
        return;
    }
    if (undoButton.bounds.contains(mousePosition)) {
        undoMove();
        return;
    }
    if (boardButton.bounds.contains(mousePosition)) {
        toggleMode();
        return;
    }
    if (aiButton.bounds.contains(mousePosition)) {
        toggleAi();
        return;
    }

    tryPlacePiece({
        static_cast<int>(std::lround(mousePosition.x)),
        static_cast<int>(std::lround(mousePosition.y))
    });
}

void Game::restart() {
    board_.reset(boardMode_);
    currentTurn_ = Board::Piece::Black;
    winner_ = Board::Piece::None;
    gameOver_ = false;
    updateWindowTitle();
}

void Game::toggleMode() {
    boardMode_ = boardMode_ == Board::Mode::Classic ? Board::Mode::Obstacle : Board::Mode::Classic;
    restart();
}

void Game::toggleAi() {
    aiEnabled_ = !aiEnabled_;
    restart();
}

void Game::undoMove() {
    const auto lastMove = board_.undoLastMove();
    if (!lastMove.has_value()) {
        return;
    }

    currentTurn_ = lastMove->piece;
    if (aiEnabled_ && lastMove->piece == Board::Piece::White) {
        const auto playerMove = board_.undoLastMove();
        if (playerMove.has_value()) {
            currentTurn_ = playerMove->piece;
        }
    }

    winner_ = Board::Piece::None;
    gameOver_ = false;
    updateWindowTitle();
}

void Game::maybeMakeAiMove() {
    if (!aiEnabled_ || gameOver_ || currentTurn_ != Board::Piece::White) {
        return;
    }

    const auto bestMove = findBestAiMove();
    if (!bestMove.has_value()) {
        gameOver_ = true;
        return;
    }

    if (!board_.placePieceAt(bestMove->x, bestMove->y, currentTurn_)) {
        return;
    }

    winner_ = board_.winnerFromLastMove();
    if (winner_ != Board::Piece::None) {
        gameOver_ = true;
    } else if (!board_.hasEmptyCell()) {
        gameOver_ = true;
    } else {
        currentTurn_ = nextTurn(currentTurn_);
    }
}

std::optional<sf::Vector2i> Game::findBestAiMove() const {
    int bestScore = std::numeric_limits<int>::min();
    std::optional<sf::Vector2i> bestMove;

    for (int row = 0; row < Board::kBoardSize; ++row) {
        for (int col = 0; col < Board::kBoardSize; ++col) {
            if (!board_.canPlaceAt(row, col)) {
                continue;
            }

            const int offense = scoreMove(row, col, Board::Piece::White);
            const int defense = scoreMove(row, col, Board::Piece::Black);
            const int centerBias = 14 - (std::abs(row - 7) + std::abs(col - 7));
            const int total = offense * 2 + defense + centerBias;

            if (total > bestScore) {
                bestScore = total;
                bestMove = sf::Vector2i(row, col);
            }
        }
    }

    return bestMove;
}

int Game::scoreMove(int row, int col, Board::Piece piece) const {
    return scoreDirection(row, col, 0, 1, piece) +
           scoreDirection(row, col, 1, 0, piece) +
           scoreDirection(row, col, 1, 1, piece) +
           scoreDirection(row, col, 1, -1, piece);
}

int Game::scoreDirection(int row, int col, int dRow, int dCol, Board::Piece piece) const {
    const int forward = countInDirection(row, col, dRow, dCol, piece);
    const int backward = countInDirection(row, col, -dRow, -dCol, piece);
    const int total = forward + backward;

    const bool openForward = isOpenEnd(row + (forward + 1) * dRow, col + (forward + 1) * dCol);
    const bool openBackward = isOpenEnd(row - (backward + 1) * dRow, col - (backward + 1) * dCol);
    const int openEnds = static_cast<int>(openForward) + static_cast<int>(openBackward);

    if (total >= 4) {
        return 100000;
    }
    if (total == 3 && openEnds == 2) {
        return 20000;
    }
    if (total == 3 && openEnds == 1) {
        return 8000;
    }
    if (total == 2 && openEnds == 2) {
        return 2500;
    }
    if (total == 2 && openEnds == 1) {
        return 900;
    }
    if (total == 1 && openEnds == 2) {
        return 300;
    }
    if (total == 1 && openEnds == 1) {
        return 120;
    }
    return openEnds > 0 ? 25 : 0;
}

int Game::countInDirection(int row, int col, int dRow, int dCol, Board::Piece piece) const {
    int count = 0;
    int currentRow = row + dRow;
    int currentCol = col + dCol;

    while (board_.pieceAt(currentRow, currentCol) == piece) {
        ++count;
        currentRow += dRow;
        currentCol += dCol;
    }

    return count;
}

bool Game::isOpenEnd(int row, int col) const {
    return board_.canPlaceAt(row, col);
}

bool Game::loadFont() {
    static constexpr std::array<const char*, 7> fontPaths = {
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simkai.ttf",
        "C:\\Windows\\Fonts\\simsunb.ttf",
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyhbd.ttc",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf"
    };

    for (const auto* fontPath : fontPaths) {
        if (std::filesystem::exists(fontPath) && uiFont_.openFromFile(fontPath)) {
            return true;
        }
    }

    return false;
}

void Game::drawMenu() {
    sf::RectangleShape heroBand;
    heroBand.setPosition({0.0f, 0.0f});
    heroBand.setSize({1280.0f, 236.0f});
    heroBand.setFillColor(sf::Color(33, 50, 83));
    window_.draw(heroBand);

    sf::RectangleShape lowerBackground;
    lowerBackground.setPosition({0.0f, 236.0f});
    lowerBackground.setSize({1280.0f, 584.0f});
    lowerBackground.setFillColor(sf::Color(17, 28, 49));
    window_.draw(lowerBackground);

    sf::CircleShape glowA(160.0f);
    glowA.setPosition({930.0f, -40.0f});
    glowA.setFillColor(sf::Color(90, 117, 173, 70));
    window_.draw(glowA);

    sf::CircleShape glowB(110.0f);
    glowB.setPosition({88.0f, 62.0f});
    glowB.setFillColor(sf::Color(199, 145, 79, 55));
    window_.draw(glowB);

    drawText("幻格五子棋", {88.0f, 66.0f}, 54, sf::Color(248, 242, 231), sf::Text::Bold);
    drawText("更适合单人课程设计的棋类游戏原型", {92.0f, 138.0f}, 24, sf::Color(198, 211, 235));
    drawText("先选择模式，再进入演示。按钮说明与玩法定位已经分开排版。", {92.0f, 176.0f}, 18,
             sf::Color(167, 181, 207));

    const UiButton classicAi = makeButton({110.0f, 286.0f}, {490.0f, 138.0f}, "经典模式 · 人机对战",
                                          sf::Color(198, 150, 73), sf::Color(255, 221, 160));
    const UiButton obstacleAi = makeButton({110.0f, 456.0f}, {490.0f, 138.0f}, "幻格模式 · 人机对战",
                                           sf::Color(93, 122, 170), sf::Color(181, 209, 255));
    const UiButton localTwoPlayer = makeButton({680.0f, 286.0f}, {490.0f, 138.0f}, "经典模式 · 双人对战",
                                               sf::Color(81, 145, 108), sf::Color(181, 237, 197));
    const UiButton exitButton = makeButton({680.0f, 456.0f}, {490.0f, 138.0f}, "退出游戏",
                                           sf::Color(138, 78, 70), sf::Color(237, 166, 156));

    const sf::Vector2f mousePosition = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
    drawButton(classicAi, isMouseOver(classicAi, mousePosition), 26);
    drawButton(obstacleAi, isMouseOver(obstacleAi, mousePosition), 26);
    drawButton(localTwoPlayer, isMouseOver(localTwoPlayer, mousePosition), 26);
    drawButton(exitButton, isMouseOver(exitButton, mousePosition), 26);

    drawText("最稳妥的起步方式，适合优先展示基础规则、胜负判定和 AI。", {142.0f, 368.0f}, 18, sf::Color(84, 55, 16));
    drawText("加入障碍格后更容易体现“创新点”，更适合课程答辩展示。", {142.0f, 538.0f}, 18, sf::Color(232, 240, 255));
    drawText("便于测试交互和核心逻辑，也适合本地双人试玩。", {712.0f, 368.0f}, 18, sf::Color(229, 245, 233));
    drawText("按 Enter 可快速开始，按 Esc 可直接退出程序。", {712.0f, 538.0f}, 18, sf::Color(255, 225, 220));

    sf::RectangleShape footerLine;
    footerLine.setPosition({88.0f, 706.0f});
    footerLine.setSize({1104.0f, 2.0f});
    footerLine.setFillColor(sf::Color(70, 90, 128));
    window_.draw(footerLine);

    drawText("游戏内快捷键：U 悔棋   O 切换棋盘   P 切换人机/双人   M 返回菜单", {92.0f, 728.0f}, 18,
             sf::Color(171, 184, 210));
}

void Game::drawGameScene() {
    sf::RectangleShape topBar;
    topBar.setPosition({0.0f, 0.0f});
    topBar.setSize({1280.0f, 112.0f});
    topBar.setFillColor(sf::Color(29, 43, 68));
    window_.draw(topBar);

    sf::RectangleShape boardCard;
    boardCard.setPosition({36.0f, 128.0f});
    boardCard.setSize({706.0f, 640.0f});
    boardCard.setFillColor(sf::Color(242, 235, 220));
    boardCard.setOutlineThickness(2.0f);
    boardCard.setOutlineColor(sf::Color(184, 164, 124));
    window_.draw(boardCard);

    sf::RectangleShape sidePanel;
    sidePanel.setPosition({772.0f, 128.0f});
    sidePanel.setSize({468.0f, 640.0f});
    sidePanel.setFillColor(sf::Color(34, 47, 72));
    sidePanel.setOutlineThickness(2.0f);
    sidePanel.setOutlineColor(sf::Color(90, 114, 155));
    window_.draw(sidePanel);

    board_.draw(window_);

    drawText("幻格五子棋", {42.0f, 20.0f}, 38, sf::Color(248, 242, 229), sf::Text::Bold);
    drawText(subtitleLine(), {46.0f, 66.0f}, 19, sf::Color(186, 202, 229));
    drawText("左侧为棋盘区域，右侧为状态与操作面板。", {46.0f, 88.0f}, 15, sf::Color(160, 176, 204));
    drawText("棋盘区", {64.0f, 146.0f}, 20, sf::Color(80, 64, 36), sf::Text::Bold);

    sf::RectangleShape statusCard;
    statusCard.setPosition({790.0f, 148.0f});
    statusCard.setSize({412.0f, 184.0f});
    statusCard.setFillColor(sf::Color(244, 241, 233));
    statusCard.setOutlineThickness(1.0f);
    statusCard.setOutlineColor(sf::Color(205, 190, 161));
    window_.draw(statusCard);

    drawText("对局状态", {816.0f, 172.0f}, 24, sf::Color(36, 48, 73), sf::Text::Bold);
    drawText(statusLine(), {816.0f, 214.0f}, 21, sf::Color(83, 63, 24));
    drawText("当前棋盘：" + modeName(), {816.0f, 258.0f}, 18, sf::Color(68, 77, 93));
    drawText("对战方式：" + playerModeName(), {816.0f, 288.0f}, 18, sf::Color(68, 77, 93));

    const UiButton menuButton = makeButton({790.0f, 382.0f}, {190.0f, 62.0f}, "返回主菜单",
                                           sf::Color(57, 76, 108), sf::Color(161, 193, 255));
    const UiButton restartButton = makeButton({1002.0f, 382.0f}, {190.0f, 62.0f}, "重新开始",
                                              sf::Color(115, 84, 47), sf::Color(236, 203, 144));
    const UiButton undoButton = makeButton({790.0f, 460.0f}, {190.0f, 62.0f}, "悔棋",
                                           sf::Color(71, 111, 82), sf::Color(177, 232, 191));
    const UiButton boardButton = makeButton({1002.0f, 460.0f}, {190.0f, 62.0f}, "切换棋盘",
                                            sf::Color(78, 97, 139), sf::Color(177, 198, 245));
    const UiButton aiButton = makeButton({790.0f, 538.0f}, {402.0f, 62.0f},
                                         aiEnabled_ ? "切换为双人对战" : "切换为人机对战",
                                         sf::Color(111, 73, 122), sf::Color(223, 173, 240));

    const sf::Vector2f mousePosition = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
    drawButton(menuButton, isMouseOver(menuButton, mousePosition), 21);
    drawButton(restartButton, isMouseOver(restartButton, mousePosition), 21);
    drawButton(undoButton, isMouseOver(undoButton, mousePosition), 21);
    drawButton(boardButton, isMouseOver(boardButton, mousePosition), 21);
    drawButton(aiButton, isMouseOver(aiButton, mousePosition), 21);

    sf::RectangleShape infoCard;
    infoCard.setPosition({790.0f, 626.0f});
    infoCard.setSize({412.0f, 108.0f});
    infoCard.setFillColor(sf::Color(43, 58, 88));
    infoCard.setOutlineThickness(1.0f);
    infoCard.setOutlineColor(sf::Color(95, 120, 164));
    window_.draw(infoCard);

    drawText("快捷操作", {816.0f, 646.0f}, 22, sf::Color(243, 240, 231), sf::Text::Bold);
    drawText("左键落子    U / Backspace 悔棋", {816.0f, 682.0f}, 16, sf::Color(204, 216, 235));
    drawText("M 返回菜单   Esc 返回 / 关闭", {816.0f, 708.0f}, 16, sf::Color(204, 216, 235));
}

void Game::drawButton(const UiButton& button, bool hovered, unsigned int characterSize) {
    sf::RectangleShape shadow;
    shadow.setPosition({button.bounds.position.x + 4.0f, button.bounds.position.y + 6.0f});
    shadow.setSize(button.bounds.size);
    shadow.setFillColor(sf::Color(0, 0, 0, hovered ? 55 : 35));
    window_.draw(shadow);

    sf::RectangleShape shape;
    shape.setPosition(button.bounds.position);
    shape.setSize(button.bounds.size);
    shape.setFillColor(button.fill);
    shape.setOutlineThickness(hovered ? 3.0f : 2.0f);
    shape.setOutlineColor(button.accent);
    window_.draw(shape);

    sf::RectangleShape accentBar;
    accentBar.setPosition(button.bounds.position);
    accentBar.setSize({button.bounds.size.x, 8.0f});
    accentBar.setFillColor(button.accent);
    window_.draw(accentBar);

    drawCenteredText(button.label, button.bounds, characterSize, sf::Color(249, 247, 241), sf::Text::Bold);
}

void Game::drawText(std::string_view text,
                    sf::Vector2f position,
                    unsigned int characterSize,
                    sf::Color color,
                    std::uint32_t style) {
    if (!fontLoaded_) {
        return;
    }

    const auto utf8 = sf::String::fromUtf8(text.begin(), text.end());
    sf::Text drawable(uiFont_, utf8, characterSize);
    drawable.setPosition(position);
    drawable.setFillColor(color);
    drawable.setStyle(style);
    window_.draw(drawable);
}

void Game::drawCenteredText(std::string_view text,
                            const sf::FloatRect& area,
                            unsigned int characterSize,
                            sf::Color color,
                            std::uint32_t style) {
    if (!fontLoaded_) {
        return;
    }

    const auto utf8 = sf::String::fromUtf8(text.begin(), text.end());
    sf::Text drawable(uiFont_, utf8, characterSize);
    drawable.setFillColor(color);
    drawable.setStyle(style);

    const auto bounds = drawable.getLocalBounds();
    drawable.setPosition({
        area.position.x + (area.size.x - bounds.size.x) * 0.5f - bounds.position.x,
        area.position.y + (area.size.y - bounds.size.y) * 0.5f - bounds.position.y - 4.0f
    });
    window_.draw(drawable);
}

Game::UiButton Game::makeButton(sf::Vector2f position,
                                sf::Vector2f size,
                                std::string label,
                                sf::Color fill,
                                sf::Color accent) const {
    return UiButton{sf::FloatRect(position, size), std::move(label), fill, accent};
}

bool Game::isMouseOver(const UiButton& button, sf::Vector2f mousePosition) const {
    return button.bounds.contains(mousePosition);
}

std::string Game::statusLine() const {
    if (winner_ == Board::Piece::Black) {
        return "黑棋获胜。";
    }
    if (winner_ == Board::Piece::White) {
        return "白棋获胜。";
    }
    if (gameOver_) {
        return "棋盘已满，本局平局。";
    }
    if (aiEnabled_ && currentTurn_ == Board::Piece::White) {
        return "AI 正在计算下一步。";
    }
    return pieceName(currentTurn_) + "回合，请落子。";
}

std::string Game::subtitleLine() const {
    return modeName() + " | " + playerModeName();
}

void Game::updateWindowTitle() {
    if (scene_ == Scene::Menu) {
        window_.setTitle(sf::String::fromUtf8("幻格五子棋 - 主菜单", "幻格五子棋 - 主菜单" + std::char_traits<char>::length("幻格五子棋 - 主菜单")));
        return;
    }

    std::string title = "幻格五子棋 - " + modeName() + " - " + playerModeName() + " - ";
    if (winner_ != Board::Piece::None) {
        title += pieceName(winner_) + "胜";
    } else if (gameOver_) {
        title += "平局";
    } else {
        title += pieceName(currentTurn_) + "回合";
    }
    title += " | U 悔棋 | O 棋盘 | P 对战方式 | M 菜单";
    window_.setTitle(sf::String::fromUtf8(title.begin(), title.end()));
}

Board::Piece Game::nextTurn(Board::Piece piece) const {
    return piece == Board::Piece::Black ? Board::Piece::White : Board::Piece::Black;
}

std::string Game::pieceName(Board::Piece piece) const {
    switch (piece) {
    case Board::Piece::Black:
        return "黑棋";
    case Board::Piece::White:
        return "白棋";
    case Board::Piece::None:
    default:
        return "无";
    }
}

std::string Game::modeName() const {
    return boardMode_ == Board::Mode::Classic ? "经典棋盘" : "幻格棋盘";
}

std::string Game::playerModeName() const {
    return aiEnabled_ ? "人机对战" : "双人对战";
}
