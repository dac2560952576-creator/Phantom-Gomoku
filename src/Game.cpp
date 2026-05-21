#include "Game.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <vector>

Game::Game()
    : window_(sf::VideoMode({1280, 820}), "Phantom Gomoku") {
    window_.setFramerateLimit(60);
    fontLoaded_ = loadFont();
    loadMenuBackground();

    // Load and start background music
    for (const auto* prefix : {"assets/", "../assets/"}) {
        const std::string musicPath = std::string(prefix) + "A Great Journey - Overworld.wav";
        if (std::filesystem::exists(musicPath)) {
            if (bgMusic_.openFromFile(musicPath)) {
                bgMusic_.setVolume(45.0f);
                bgMusic_.setLooping(true);
                bgMusic_.play();
                currentMusic_ = &bgMusic_;
                currentMusicVol_ = 45.0f;
            }
            break;
        }
    }

    // Load difficulty-based gameplay music
    {
        bool found = false;
        for (const auto* prefix : {"assets/", "../assets/"}) {
            auto tryLoad = [&](sf::Music& music, const std::string& filename, float vol) {
                const std::string path = std::string(prefix) + filename;
                if (std::filesystem::exists(path)) {
                    found = true;
                    if (music.openFromFile(path)) {
                        music.setLooping(true);
                        music.setVolume(vol);
                    }
                }
            };
            tryLoad(easyMusic_, "Red Leaf Town - rest area.wav", 50.0f);
            tryLoad(mediumMusic_, "The Merchant - Shop.wav", 50.0f);
            tryLoad(hardMusic_, "The Beast's Lair - Boss Fight.wav", 55.0f);
            if (found) break;
        }
    }

    // Load card event music
    for (const auto* prefix : {"assets/", "../assets/"}) {
        const std::string evPath = std::string(prefix) + "01-8bit01.mp3";
        if (std::filesystem::exists(evPath) && eventMusic_.openFromFile(evPath)) {
            eventMusic_.setLooping(true);
            eventMusic_.setVolume(65.0f);
            break;
        }
    }

    // Load piece placement sound (using sf::Music for mp3 compatibility)
    for (const auto* prefix : {"assets/", "../assets/"}) {
        const std::string sfxPath = std::string(prefix) + "9.mp3";
        if (std::filesystem::exists(sfxPath) && placeSfx_.openFromFile(sfxPath)) {
            placeSfx_.setLooping(false);
            placeSfx_.setVolume(200.0f);
            break;
        }
    }

    // Load button click sound
    for (const auto* prefix : {"assets/", "../assets/"}) {
        const std::string sfxPath = std::string(prefix) + "UI_button02.wav";
        if (std::filesystem::exists(sfxPath)) {
            if (btnSoundBuffer_.loadFromFile(sfxPath)) {
                btnSound_.emplace(btnSoundBuffer_);
                btnSound_->setVolume(200.0f);
            }
            break;
        }
    }

    // Load win/lose sounds
    for (const auto* prefix : {"assets/", "../assets/"}) {
        const std::string winPath = std::string(prefix) + "Win vol. 1.wav";
        const std::string losePath = std::string(prefix) + "Lose vol. 1.wav";
        if (std::filesystem::exists(winPath) && winSoundBuffer_.loadFromFile(winPath)) {
            winSound_.emplace(winSoundBuffer_);
            winSound_->setVolume(100.0f);
        }
        if (std::filesystem::exists(losePath) && loseSoundBuffer_.loadFromFile(losePath)) {
            loseSound_.emplace(loseSoundBuffer_);
            loseSound_->setVolume(100.0f);
        }
        break;
    }

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
            if (scene_ == Scene::NetworkWait && !isNetworkHost_) {
                if (keyPressed->scancode == sf::Keyboard::Scancode::Backspace) {
                    if (!joinIp_.empty()) {
                        joinIp_.pop_back();
                    }
                }
            }

            if (keyPressed->scancode == sf::Keyboard::Scancode::Escape) {
                if (transitionState_ != TransitionState::None) {
                    // Ignore input during transition
                } else if (scene_ == Scene::Playing) {
                    startTransition([this] { stopNetwork(); scene_ = Scene::ModeSelect; updateWindowTitle(); });
                } else if (scene_ == Scene::NetworkWait) {
                    stopNetwork();
                    scene_ = Scene::NetworkLobby;
                } else if (scene_ == Scene::NetworkLobby) {
                    scene_ = Scene::ModeSelect;
                } else if (scene_ == Scene::DifficultySelect) {
                    scene_ = Scene::ModeSelect;
                } else if (scene_ == Scene::ModeSelect || scene_ == Scene::Rules) {
                    scene_ = Scene::MainMenu;
                } else {
                    window_.close();
                }
                updateWindowTitle();
                continue;
            }

            if (scene_ == Scene::MainMenu) {
                if (keyPressed->scancode == sf::Keyboard::Scancode::Enter) {
                    scene_ = Scene::ModeSelect;
                    updateWindowTitle();
                }
                continue;
            }

            if (scene_ == Scene::Playing) {
                if (transitionState_ != TransitionState::None) continue;
                if (keyPressed->scancode == sf::Keyboard::Scancode::R) {
                    if (networkMode_) {
                        if (server_) {
                            server_->sendRestart();
                        } else if (client_) {
                            client_->sendRestart();
                        }
                    }
                    restart();
                    if (server_ && boardMode_ == Board::Mode::Obstacle) {
                        server_->sendObstacleSync(board_.obstaclePositions());
                    }
                } else if (keyPressed->scancode == sf::Keyboard::Scancode::U ||
                           keyPressed->scancode == sf::Keyboard::Scancode::Backspace) {
                    if (networkMode_) {
                        undoNetworkMove();
                    } else {
                        undoMove();
                    }
                } else if (keyPressed->scancode == sf::Keyboard::Scancode::M) {
                    startTransition([this] {
                        if (networkMode_) stopNetwork();
                        scene_ = Scene::MainMenu;
                        updateWindowTitle();
                    });
                }
            }
        }

        if (scene_ == Scene::NetworkWait && !isNetworkHost_ && ipInputActive_) {
            if (const auto* textEntered = event->getIf<sf::Event::TextEntered>()) {
                const auto ch = textEntered->unicode;
                if (ch == '\b') {
                    if (!joinIp_.empty()) joinIp_.pop_back();
                } else if ((ch >= '0' && ch <= '9') || ch == '.') {
                    if (joinIp_.size() < 18) {
                        joinIp_ += static_cast<char>(ch);
                    }
                }
            }
        }

        if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mousePressed->button != sf::Mouse::Button::Left) {
                continue;
            }

            const sf::Vector2f mousePosition{
                static_cast<float>(mousePressed->position.x),
                static_cast<float>(mousePressed->position.y)
            };

            if (transitionState_ != TransitionState::None) return;

            // Route to card selection if event is active
            if (cardEventState_ == CardEventState::Choosing) {
                handleCardSelectionClick(mousePosition);
                continue;
            }

            // Messenger wait — click anywhere to proceed
            if (cardEventState_ == CardEventState::MessengerWait) {
                if (networkMode_) {
                    // Server drives the wheel: generates angle, sends to client, starts spin
                    if (server_) {
                        cardEventState_ = CardEventState::WheelSpin;
                        wheelSpinClock_.restart();
                        wheelAngle_ = 0.0f;
                        wheelStartAngle_ = static_cast<float>(std::rand() % 360);
                        wheelResultHost_ = false;
                        wheelResultSet_ = false;
                        wheelStopped_ = false;
                        iAmPicker_ = false;
                        server_->sendWheelStartAngle(static_cast<int>(wheelStartAngle_));
                    }
                    // Client waits for 0x0B from server — transition happens in processNetworkEvents
                } else {
                    cardEventState_ = CardEventState::DealCards;
                    cardDealProgress_ = 0.0f;
                }
                cardEventClock_.restart();
                cardEventSpeech_.clear();
                continue;
            }

            const int prevAnimIndex = buttonAnimIndex_;
            switch (scene_) {
            case Scene::MainMenu:
                handleMainMenuClick(mousePosition);
                break;
            case Scene::Rules:
                handleRulesClick(mousePosition);
                break;
            case Scene::ModeSelect:
                handleModeSelectClick(mousePosition);
                break;
            case Scene::DifficultySelect:
                handleDifficultySelectClick(mousePosition);
                break;
            case Scene::NetworkLobby:
                handleNetworkLobbyClick(mousePosition);
                break;
            case Scene::RoomSetup:
                handleRoomSetupClick(mousePosition);
                break;
            case Scene::NetworkWait:
                handleNetworkWaitClick(mousePosition);
                break;
            case Scene::Playing:
                handleGameClick(mousePosition);
                break;
            }
            // Only play button sound when a button was actually pressed
            if (buttonAnimIndex_ >= 0 && prevAnimIndex < 0) {
                playButtonSound();
            }
        }
    }
}

void Game::update() {
    // Background music management (only on scene transitions)
    if (scene_ != lastScene_) {
        if (lastScene_ == Scene::Playing) {
            // Cancel any win/lose fast fade
            if (outgoingMusic_ && !currentMusic_) {
                outgoingMusic_->stop();
                outgoingMusic_ = nullptr;
            }
            winLoseTriggered_ = false;
        }
        lastScene_ = scene_;

        sf::Music* target = nullptr;
        float targetVol = 0.0f;

        if (scene_ == Scene::Playing) {
            if (!networkMode_) {
                // Local 2P or Easy AI → Red Leaf; Medium → Merchant; Hard → Beast's Lair
                target = &easyMusic_;
                targetVol = 50.0f;
                if (aiEnabled_) {
                    if (aiDifficulty_ == AIDifficulty::Medium) { target = &mediumMusic_; targetVol = 50.0f; }
                    else if (aiDifficulty_ == AIDifficulty::Hard) { target = &hardMusic_; targetVol = 55.0f; }
                }
            } else {
                // Network mode: Classic→Red Leaf, Obstacle Easy→Merchant, Obstacle Hard→Beast's Lair
                if (boardMode_ == Board::Mode::Classic) {
                    target = &easyMusic_; targetVol = 50.0f;
                } else if (!obstacleDynamic_) {
                    target = &mediumMusic_; targetVol = 50.0f;
                } else {
                    target = &hardMusic_; targetVol = 55.0f;
                }
            }
        } else if (scene_ != Scene::Playing) {
            target = &bgMusic_;
            targetVol = 45.0f;
        }

        if (target == currentMusic_ && !outgoingMusic_) {
            // Same track, no transition needed
        } else {
            startCrossfade(target, targetVol);
        }
    }

    // Crossfade progress (skip during win/lose fast fade)
    if (outgoingMusic_ && !winLoseTriggered_) {
        const float progress = std::min(1.0f, crossfadeClock_.getElapsedTime().asSeconds() / kCrossfadeDuration);
        // Quadratic fade-out (fast drop), ease-in-out fade-in
        const float tOut = 1.0f - (1.0f - progress) * (1.0f - progress);
        const float tIn = progress < 0.5f ? 2.0f * progress * progress : 1.0f - std::pow(-2.0f * progress + 2.0f, 2.0f) / 2.0f;
        outgoingMusic_->setVolume(outgoingStartVol_ * (1.0f - tOut));
        if (currentMusic_) {
            currentMusic_->setVolume(currentMusicVol_ * tIn);
        }
        if (progress >= 1.0f) {
            outgoingMusic_->stop();
            outgoingMusic_->setVolume(0.0f);
            outgoingMusic_ = nullptr;
            if (currentMusic_) {
                currentMusic_->setVolume(currentMusicVol_);
            }
        }
    }

    // Win/lose sound trigger
    if (gameOver_ && !winLoseTriggered_) {
        winLoseTriggered_ = true;

        // Determine win/lose from local perspective
        bool iWon = false;
        if (networkMode_) {
            iWon = (isNetworkHost_ && winner_ == Board::Piece::Black) ||
                   (!isNetworkHost_ && winner_ == Board::Piece::White);
        } else {
            iWon = (winner_ == Board::Piece::Black);
        }

        if (winner_ != Board::Piece::None) {
            if (iWon && winSound_.has_value()) {
                winSound_->play();
            } else if (!iWon && loseSound_.has_value()) {
                loseSound_->play();
            }
        }

        // Fast fade-out of current BGM
        if (currentMusic_ && currentMusic_->getStatus() == sf::Music::Status::Playing) {
            outgoingMusic_ = currentMusic_;
            outgoingStartVol_ = currentMusicVol_;
            currentMusic_ = nullptr;
            winLoseFadeClock_.restart();
        }
    }

    // Fast fade-out for win/lose (separate from normal crossfade)
    if (outgoingMusic_ && !currentMusic_ && winLoseTriggered_) {
        const float progress = std::min(1.0f, winLoseFadeClock_.getElapsedTime().asSeconds() / kWinLoseFadeDuration);
        const float tOut = 1.0f - (1.0f - progress) * (1.0f - progress); // quadratic fast drop
        outgoingMusic_->setVolume(outgoingStartVol_ * (1.0f - tOut));
        if (progress >= 1.0f) {
            outgoingMusic_->stop();
            outgoingMusic_->setVolume(0.0f);
            outgoingMusic_ = nullptr;
            winLoseTriggered_ = false;
        }
    }

    // Scene transition (fade to black + pixel dissolve)
    if (transitionState_ != TransitionState::None) {
        const float elapsed = transitionClock_.getElapsedTime().asSeconds();
        if (transitionState_ == TransitionState::FadingOut) {
            transitionAlpha_ = std::min(elapsed / kTransitionDuration, 1.0f);
            if (transitionAlpha_ >= 1.0f) {
                transitionAction_();
                transitionAction_ = nullptr;
                transitionState_ = TransitionState::FadingIn;
                transitionClock_.restart();
            }
        } else {
            transitionAlpha_ = 1.0f - std::min(elapsed / kTransitionDuration, 1.0f);
            if (transitionAlpha_ <= 0.0f) {
                transitionAlpha_ = 0.0f;
                transitionState_ = TransitionState::None;
                if (scene_ == Scene::Playing) {
                    gameReady_ = true;
                    gameIntroAlpha_ = 0.0f;
                }
            }
        }
    }

    // Game intro fade-in (UI elements emerge after clicking to start)
    if (scene_ == Scene::Playing && !gameReady_ && gameIntroAlpha_ < 1.0f && transitionState_ == TransitionState::None) {
        gameIntroAlpha_ = std::min(1.0f, gameIntroClock_.getElapsedTime().asSeconds() / kGameIntroDuration);
    }

    // Card event state transitions
    // Card event music transitions
    if (cardEventState_ != lastCardEventState_) {
        if (lastCardEventState_ == CardEventState::Idle &&
            cardEventState_ != CardEventState::Idle &&
            cardEventState_ != CardEventState::Choosing) {
            // Event started: save game music, crossfade to event music
            savedGameMusic_ = currentMusic_;
            savedGameMusicVol_ = currentMusicVol_;
            startCrossfade(&eventMusic_, 65.0f);
        } else if (cardEventState_ == CardEventState::Idle && lastCardEventState_ != CardEventState::Idle) {
            // Event ended: crossfade back to saved game music
            startCrossfade(savedGameMusic_, savedGameMusicVol_);
        }
        lastCardEventState_ = cardEventState_;
    }

    if (cardEventState_ != CardEventState::Idle && cardEventState_ != CardEventState::Choosing) {
        const float cardElapsed = cardEventClock_.getElapsedTime().asSeconds();

        if (cardEventState_ == CardEventState::Applied && cardElapsed >= 0.5f) {
            cardEventState_ = CardEventState::Idle;
            if (cardDeferredTurn_) {
                cardDeferredTurn_ = false;
                currentTurn_ = nextTurn(currentTurn_);
            }
            if (aiEnabled_ && !gameOver_ && currentTurn_ == Board::Piece::White) {
                aiMovePending_ = true;
                aiThinkTimeMs_ = 400 + std::rand() % 600;
                aiClock_.restart();
            }
            turnTimer_.restart();
            updateWindowTitle();
        }

        if (cardEventState_ == CardEventState::Omen) {
            constexpr float kOmenDuration = 3.0f;
            const float prog = std::min(1.0f, cardElapsed / kOmenDuration);
            // Shake: gradual ramp up then fade down
            shakeIntensity_ = prog < 0.15f ? (prog / 0.15f) * 3.0f :
                              prog < 0.55f ? 3.0f + (prog - 0.15f) / 0.4f * 9.0f :
                              (1.0f - prog) / 0.45f * 6.0f;
            darkenAlpha_ = std::min(0.78f, prog * 0.78f / 0.6f);
            if (prog > 0.3f && speechText_.empty()) {
                speechText_ = aiDifficulty_ == AIDifficulty::Hard ? "等等...这股力量是？！" : "哇！有什么东西要来了！";
                speechClock_.restart();
            }
            if (prog >= 1.0f) {
                cardEventState_ = CardEventState::MessengerBig;
                cardEventClock_.restart();
                messengerAlpha_ = 0.0f;
                messengerPos_ = {640.0f, 520.0f};
                messengerFloatClock_.restart();
                speechText_.clear();
            }
        }

        if (cardEventState_ == CardEventState::MessengerBig) {
            constexpr float kBigDuration = 2.5f;
            const float prog = std::min(1.0f, cardElapsed / kBigDuration);
            messengerAlpha_ = std::min(1.0f, prog / 0.35f);
            if (prog > 0.55f && cardEventSpeech_.empty()) {
                cardEventSpeech_ = "大事件要发生了\n你准备好了吗？";
            }
            if (prog >= 1.0f) {
                cardEventState_ = CardEventState::MessengerWait;
                cardEventClock_.restart();
            }
        }

        // MessengerWait stays until click in processEvents

        if (cardEventState_ == CardEventState::DealCards) {
            constexpr float kDealDuration = 1.6f;
            const float prog = std::min(1.0f, cardElapsed / kDealDuration);
            cardDealProgress_ = 1.0f - (1.0f - prog) * (1.0f - prog) * (1.0f - prog); // ease-out cubic
            // Messenger stays at center
            if (prog >= 1.0f) {
                cardEventState_ = CardEventState::FlipCards;
                cardEventClock_.restart();
                cardFlipProgress_ = 0.0f;
            }
        }

        if (cardEventState_ == CardEventState::FlipCards) {
            constexpr float kFlipDuration = 0.7f;
            const float prog = std::min(1.0f, cardElapsed / kFlipDuration);
            cardFlipProgress_ = prog;
            if (prog >= 1.0f) {
                cardEventState_ = CardEventState::Choosing;
                cardEventClock_.restart();
                cardFloatTime_ = 0.0f;
                cardEventSpeech_.clear();
            }
        }

        // Network wheel spin: determines who picks the card
        if (cardEventState_ == CardEventState::WheelSpin) {
            constexpr float kWheelDuration = 3.5f;
            constexpr float kMaxSpeed = 720.0f;  // degrees per second at start
            const float wElapsed = wheelSpinClock_.getElapsedTime().asSeconds();
            const float wProg = std::min(1.0f, wElapsed / kWheelDuration);

            // Frame-rate independent: analytical integral of kMaxSpeed*(1-t/T)^2
            const float t = std::min(wElapsed, kWheelDuration);
            constexpr float T = kWheelDuration;
            wheelAngle_ = wheelStartAngle_ + kMaxSpeed * (t - t*t/T + t*t*t/(3.0f*T*T));

            if (wProg >= 1.0f) {
                wheelStopped_ = true;

                // Server determines and sends result (once)
                if (server_ && !wheelResultSet_) {
                    const float normalizedAngle = std::fmod(wheelAngle_, 360.0f);
                    wheelResultHost_ = normalizedAngle < 180.0f;
                    wheelResultSet_ = true;
                    iAmPicker_ = wheelResultHost_;
                    server_->sendWheelResult(wheelResultHost_);
                }
                // Single-player safety path
                if (!server_ && !client_ && !wheelResultSet_) {
                    const float normalizedAngle = std::fmod(wheelAngle_, 360.0f);
                    wheelResultHost_ = normalizedAngle < 180.0f;
                    wheelResultSet_ = true;
                    iAmPicker_ = wheelResultHost_;
                }

                // After result is known + 2s display, proceed to deal cards
                if (wheelResultSet_ && wElapsed >= kWheelDuration + 2.0f) {
                    cardEventState_ = CardEventState::DealCards;
                    cardEventClock_.restart();
                    cardDealProgress_ = 0.0f;
                    cardEventSpeech_.clear();
                }
            }
        }

        if (cardEventState_ == CardEventState::Reveal) {
            // Plan A: 聚焦升空 — 5 sub-phases (Confirm→Eliminate→Ascend→Bloom→Dissipate)
            constexpr float kRevealDuration = 1.75f;
            constexpr float kAscendEnd = 1.05f;
            constexpr float kBloomEnd = 1.45f;

            const float prog = std::min(1.0f, cardElapsed / kRevealDuration);
            cardRevealProgress_ = prog;

            // Bloom spark spawn — once when entering bloom
            static bool bloomSparksSpawned = false;
            if (cardElapsed >= kAscendEnd && !bloomSparksSpawned) {
                bloomSparksSpawned = true;
                const sf::Vector2f center{640.0f, 410.0f};
                constexpr int kBloomSparkCount = 28;
                for (int i = 0; i < kBloomSparkCount; ++i) {
                    const float angle = static_cast<float>(i) / static_cast<float>(kBloomSparkCount) * 6.283185f;
                    const float speed = 60.0f + static_cast<float>(std::rand() % 100);
                    const float life = 0.5f + static_cast<float>(std::rand() % 100) / 100.0f * 0.4f;
                    Spark sp;
                    sp.pos = center;
                    sp.vel = {std::cos(angle) * speed, std::sin(angle) * speed};
                    sp.life = life;
                    sp.maxLife = life;
                    sp.color = sf::Color(255, 220, 60, 255);
                    sp.size = 2.5f + static_cast<float>(std::rand() % 100) / 100.0f * 3.0f;
                    sparks_.push_back(sp);
                }
            }

            // Messenger speaks during bloom
            if (cardElapsed > kAscendEnd + 0.15f && cardEventSpeech_.empty()) {
                cardEventSpeech_ = "不错的决定，命运已然注定...";
            }

            // Fade dark overlay and messenger during dissipate
            if (cardElapsed >= kBloomEnd) {
                const float dissipateProg = std::min(1.0f, (cardElapsed - kBloomEnd) / (kRevealDuration - kBloomEnd));
                darkenAlpha_ = 0.78f * (1.0f - dissipateProg);
                messengerAlpha_ = 0.35f * (1.0f - dissipateProg);
                shakeIntensity_ = 0.0f;
            }

            if (prog >= 1.0f) {
                applyCardEffect(drawnCards_[chosenCardIdx_]);
                cardEventState_ = CardEventState::Applied;
                cardEventClock_.restart();
                messengerAlpha_ = 0.0f;
                darkenAlpha_ = 0.0f;
                shakeIntensity_ = 0.0f;
                cardEventSpeech_.clear();
                bloomSparksSpawned = false;
            }
        }
    }

    // Card float time during Choosing
    if (cardEventState_ == CardEventState::Choosing) {
        cardFloatTime_ = cardEventClock_.getElapsedTime().asSeconds();
        if (cardFloatTime_ > 2.0f && cardEventSpeech_.empty()) {
            cardEventSpeech_ = "谨慎选择，命运不可更改...";
        }
    }

    // Death card animation (post-card-event): Spread → PreFlash → Flash → Consume → Fade
    if (deathAnimPending_) {
        const float dElapsed = deathAnimClock_.getElapsedTime().asSeconds();
        constexpr float kSpreadInterval = 0.06f;
        constexpr float kSpreadEnd = kSpreadInterval * 9.0f;   // 0.54s
        constexpr float kPreFlashEnd = kSpreadEnd + 0.12f;      // 0.66s
        constexpr float kFlashEnd = kPreFlashEnd + 0.35f;       // 1.01s
        constexpr float kConsumeEnd = kFlashEnd + 0.5f;         // 1.51s
        constexpr float kFadeEnd = kConsumeEnd + 0.3f;          // 1.81s

        if (dElapsed < kSpreadEnd) {
            deathSpreadStep_ = std::min(8, static_cast<int>(dElapsed / kSpreadInterval));
            deathFlashAlpha_ = 0.0f;
            deathConsumeProgress_ = 0.0f;
            // Screen shake grows with spread count
            shakeIntensity_ = deathSpreadStep_ * 0.6f;
        } else if (dElapsed < kPreFlashEnd) {
            // Anticipation: hold spread, darken, build tension
            deathSpreadStep_ = 9;
            const float ap = (dElapsed - kSpreadEnd) / 0.12f;
            deathFlashAlpha_ = ap * 40.0f; // dim glow rising
            deathConsumeProgress_ = 0.0f;
            shakeIntensity_ = 5.0f + ap * 1.5f;
        } else if (dElapsed < kFlashEnd) {
            deathSpreadStep_ = 9;
            const float fp = (dElapsed - kPreFlashEnd) / 0.35f;
            deathFlashAlpha_ = std::sin(fp * 3.14159265f) * 220.0f;
            deathConsumeProgress_ = 0.0f;
            shakeIntensity_ = std::sin(fp * 3.14159265f) * 6.5f;
        } else if (dElapsed < kConsumeEnd) {
            // Collect pieces and remove from board on first frame of consume
            if (deathPieces_.empty()) {
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        const int r = deathCenter_.x + dr;
                        const int c = deathCenter_.y + dc;
                        if (r < 0 || r >= Board::kBoardSize || c < 0 || c >= Board::kBoardSize) continue;
                        const auto p = board_.pieceAt(r, c);
                        if (p != Board::Piece::None) {
                            deathPieces_.push_back({board_.cellToPixel(r, c), p});
                        }
                        board_.removePieceAt(r, c);
                    }
                }
            }
            deathConsumeProgress_ = std::min(1.0f, (dElapsed - kFlashEnd) / 0.5f);
            deathFlashAlpha_ = 200.0f * (1.0f - deathConsumeProgress_);
            shakeIntensity_ = 2.5f * (1.0f - deathConsumeProgress_);
        } else if (dElapsed < kFadeEnd) {
            const float fp = (dElapsed - kConsumeEnd) / 0.3f;
            deathFlashAlpha_ = 0.0f;
            deathConsumeProgress_ = 1.0f - fp; // ripple fades
            shakeIntensity_ = 0.0f;
        } else {
            // Cleanup
            deathPieces_.clear();
            winner_ = Board::Piece::None;
            gameOver_ = false;
            deathAnimPending_ = false;
            deathFlashAlpha_ = 0.0f;
            deathSpreadStep_ = 0;
            deathConsumeProgress_ = 0.0f;
            shakeIntensity_ = 0.0f;
        }
    }

    // Lovers card animation: Reveal → Rise → Flight → Landing → Settle
    if (loversAnimPending_) {
        const float lElapsed = loversAnimClock_.getElapsedTime().asSeconds();
        constexpr float kRevealEnd = 0.45f;
        constexpr float kRiseEnd = 0.75f;
        constexpr float kFlightEnd = 1.45f;
        constexpr float kLandingEnd = 1.85f;
        constexpr float kSettleEnd = 2.20f;

        const auto pxA = board_.cellToPixel(loversPieceA_.x, loversPieceA_.y);
        const auto pxB = board_.cellToPixel(loversPieceB_.x, loversPieceB_.y);

        auto spawnHeart = [&](sf::Vector2f origin, float spreadRad) {
            static thread_local std::mt19937 rng(std::random_device{}());
            LoversHeartParticle hp;
            const float angle = std::uniform_real_distribution<float>(0.0f, 2.0f * 3.14159265f)(rng);
            const float speed = std::uniform_real_distribution<float>(18.0f, 55.0f)(rng);
            hp.pos = origin + sf::Vector2f{
                std::uniform_real_distribution<float>(-spreadRad, spreadRad)(rng),
                std::uniform_real_distribution<float>(-spreadRad, spreadRad)(rng)};
            hp.vel = {std::cos(angle) * speed, std::sin(angle) * speed - 25.0f};
            hp.life = 0.0f;
            hp.maxLife = std::uniform_real_distribution<float>(0.6f, 1.5f)(rng);
            hp.scale = std::uniform_real_distribution<float>(0.4f, 1.3f)(rng);
            hp.rotation = std::uniform_real_distribution<float>(0.0f, 360.0f)(rng);
            hp.rotSpeed = std::uniform_real_distribution<float>(-200.0f, 200.0f)(rng);
            loversHeartParticles_.push_back(hp);
        };

        if (lElapsed < kRevealEnd) {
            // Phase 0: Reveal — pieces pulse at origin, heart glows build
            const float rp = lElapsed / kRevealEnd;
            loversArcProgress_ = 0.0f;
            loversFloatingTextAlpha_ = rp * 0.3f;
            if (static_cast<int>(lElapsed * 45.0f) % 3 == 0) {
                spawnHeart(pxA, 20.0f);
                spawnHeart(pxB, 20.0f);
            }
        } else if (lElapsed < kRiseEnd) {
            // Phase 1: Rise — pieces lift upward, trails begin
            const float rp = (lElapsed - kRevealEnd) / (kRiseEnd - kRevealEnd);
            const float eased = rp * rp * (3.0f - 2.0f * rp);
            loversArcProgress_ = eased * 0.05f;
            loversFloatingTextAlpha_ = 0.3f + eased * 0.7f;
            if (static_cast<int>(lElapsed * 35.0f) % 2 == 0) {
                spawnHeart({pxA.x + (pxB.x - pxA.x) * eased * 0.4f,
                           pxA.y + (pxB.y - pxA.y) * eased * 0.4f - eased * 20.0f}, 14.0f);
                spawnHeart({pxB.x + (pxA.x - pxB.x) * eased * 0.4f,
                           pxB.y + (pxA.y - pxB.y) * eased * 0.4f - eased * 20.0f}, 14.0f);
            }
        } else if (lElapsed < kFlightEnd) {
            // Phase 2: Flight — pieces traverse bezier arcs
            const float fp = (lElapsed - kRiseEnd) / (kFlightEnd - kRiseEnd);
            const float eased = fp < 0.5f
                ? 2.0f * fp * fp
                : 1.0f - std::pow(-2.0f * fp + 2.0f, 2.0f) / 2.0f;
            loversArcProgress_ = 0.05f + eased * 0.95f;
            loversFloatingTextAlpha_ = 1.0f;
            const int arcIdx = std::clamp(static_cast<int>(eased * 35.0f), 0, 35);
            if (static_cast<int>(lElapsed * 65.0f) % 3 == 0) {
                spawnHeart(loversArcPathA_[arcIdx], 9.0f);
                spawnHeart(loversArcPathB_[arcIdx], 9.0f);
            }
            if (static_cast<int>(lElapsed * 85.0f) % 6 == 0) {
                spawnSparks(loversArcPathA_[arcIdx], loversPieceAType_);
                spawnSparks(loversArcPathB_[arcIdx],
                    loversPieceAType_ == Board::Piece::Black ? Board::Piece::White : Board::Piece::Black);
            }
        } else if (lElapsed < kLandingEnd) {
            // Phase 3: Landing — burst explosion at midpoint
            const float lp = (lElapsed - kFlightEnd) / (kLandingEnd - kFlightEnd);
            loversArcProgress_ = 1.0f;
            // Spawn burst on first frame of landing
            if (lp < 0.045f) {
                for (int i = 0; i < 30; ++i) spawnHeart(loversMidpoint_, 4.0f);
                shakeIntensity_ = 5.0f;
            }
            loversFloatingTextAlpha_ = std::max(0.0f, 1.0f - lp * 0.9f);
            shakeIntensity_ = std::max(shakeIntensity_, 5.0f * (1.0f - lp));
        } else if (lElapsed < kSettleEnd) {
            // Phase 4: Settle — everything calms down
            const float sp = (lElapsed - kLandingEnd) / (kSettleEnd - kLandingEnd);
            loversArcProgress_ = 1.0f;
            loversFloatingTextAlpha_ = std::max(0.0f, 0.6f * (1.0f - sp));
            shakeIntensity_ = std::max(0.0f, shakeIntensity_ * 0.85f - 0.1f);
        } else {
            // Cleanup: place pieces at swapped positions
            const auto otherType = (loversPieceAType_ == Board::Piece::Black)
                ? Board::Piece::White : Board::Piece::Black;
            board_.placePieceAt(loversPieceB_.x, loversPieceB_.y, loversPieceAType_);
            board_.placePieceAt(loversPieceA_.x, loversPieceA_.y, otherType);
            loversAnimPending_ = false;
            loversHeartParticles_.clear();
            loversArcPathA_.clear();
            loversArcPathB_.clear();
            loversArcProgress_ = 0.0f;
            loversFloatingTextAlpha_ = 0.0f;
            shakeIntensity_ = 0.0f;
        }

        // Update all heart particles
        constexpr float kDt = 1.0f / 60.0f;
        for (auto& hp : loversHeartParticles_) {
            hp.life += kDt;
            hp.pos += hp.vel * kDt;
            hp.vel.y += 18.0f * kDt;
            hp.rotation += hp.rotSpeed * kDt;
        }
        loversHeartParticles_.erase(
            std::remove_if(loversHeartParticles_.begin(), loversHeartParticles_.end(),
                [](const LoversHeartParticle& hp) { return hp.life >= hp.maxLife; }),
            loversHeartParticles_.end());
    }

    // World card animation: Descent → Expansion → Convergence → Seal → Dissipate
    if (worldAnimPending_) {
        const float wElapsed = worldAnimClock_.getElapsedTime().asSeconds();
        constexpr float kDescentEnd = 0.55f;
        constexpr float kExpandEnd = 1.3f;
        constexpr float kConvergeEnd = 2.0f;
        constexpr float kSealEnd = 2.5f;
        constexpr float kDissipateEnd = 3.0f;
        static thread_local std::mt19937 rng(std::random_device{}());

        const float boardCx = board_.cellToPixel(Board::kBoardSize / 2, Board::kBoardSize / 2).x;
        const float boardCy = board_.cellToPixel(Board::kBoardSize / 2, Board::kBoardSize / 2).y;

        auto spawnWp = [&](sf::Vector2f origin, float spread, float spdMin, float spdMax) {
            const float a = std::uniform_real_distribution<float>(0.0f, 2.0f * 3.14159265f)(rng);
            const float s = std::uniform_real_distribution<float>(spdMin, spdMax)(rng);
            WorldParticle wp;
            wp.pos = origin + sf::Vector2f{std::uniform_real_distribution<float>(-spread, spread)(rng),
                                           std::uniform_real_distribution<float>(-spread, spread)(rng)};
            wp.vel = {std::cos(a) * s, std::sin(a) * s};
            wp.life = 0.0f;
            wp.maxLife = std::uniform_real_distribution<float>(0.5f, 1.6f)(rng);
            wp.scale = std::uniform_real_distribution<float>(0.5f, 1.6f)(rng);
            wp.rotation = std::uniform_real_distribution<float>(0.0f, 360.0f)(rng);
            wp.rotSpeed = std::uniform_real_distribution<float>(-150.0f, 150.0f)(rng);
            worldParticles_.push_back(wp);
        };

        if (wElapsed < kDescentEnd) {
            // Phase 0: Mandala descends from above, scaling up
            const float dp = wElapsed / kDescentEnd;
            const float eased = 1.0f - (1.0f - dp) * (1.0f - dp);
            mandalaScale_ = 0.2f + eased * 0.8f;
            mandalaAlpha_ = dp * 0.92f;
            mandalaRotation_ += 0.7f * (1.0f / 60.0f);
            mandalaCenter_ = {boardCx, -50.0f + eased * (boardCy + 50.0f)};
            amberVignetteAlpha_ = dp * 0.15f;
            if (static_cast<int>(wElapsed * 40.0f) % 4 == 0)
                spawnWp(mandalaCenter_, 20.0f, 8.0f, 28.0f);
        } else if (wElapsed < kExpandEnd) {
            // Phase 1: 5 concentric rings expand outward
            const float ep = (wElapsed - kDescentEnd) / (kExpandEnd - kDescentEnd);
            mandalaScale_ = 1.0f + std::sin(ep * 3.14159265f * 1.6f) * 0.06f;
            mandalaAlpha_ = 0.92f;
            mandalaRotation_ += 1.3f * (1.0f / 60.0f);
            mandalaCenter_ = {boardCx, boardCy};
            amberVignetteAlpha_ = 0.15f + ep * 0.08f;
            for (int i = 0; i < 5; ++i) {
                const float stagger = static_cast<float>(i) * 0.1f;
                const float rp = std::clamp((ep - stagger) / (1.0f - stagger + 0.01f), 0.0f, 1.0f);
                const float easedR = 1.0f - (1.0f - rp) * (1.0f - rp);
                ringRadii_[i] = easedR * (55.0f + static_cast<float>(i) * 52.0f);
                ringAlphas_[i] = rp > 0.01f ? 0.78f * (1.0f - rp * 0.25f) : 0.0f;
            }
            if (static_cast<int>(wElapsed * 55.0f) % 3 == 0) {
                for (int i = 0; i < 5; ++i) {
                    if (ringRadii_[i] > 6.0f && ringAlphas_[i] > 0.12f) {
                        const float a = std::uniform_real_distribution<float>(0.0f, 2.0f * 3.14159265f)(rng);
                        spawnWp({mandalaCenter_.x + std::cos(a) * ringRadii_[i],
                                 mandalaCenter_.y + std::sin(a) * ringRadii_[i]}, 4.0f, 4.0f, 18.0f);
                    }
                }
            }
        } else if (wElapsed < kConvergeEnd) {
            // Phase 2: Rings converge from board center toward seal target
            const float cp = (wElapsed - kExpandEnd) / (kConvergeEnd - kExpandEnd);
            const float eased = cp < 0.5f
                ? 2.0f * cp * cp : 1.0f - std::pow(-2.0f * cp + 2.0f, 2.0f) / 2.0f;
            mandalaScale_ = 1.0f - eased * 0.55f;
            mandalaAlpha_ = 0.92f - eased * 0.55f;
            mandalaRotation_ += (1.3f - eased * 0.7f) * (1.0f / 60.0f);
            mandalaCenter_ = {boardCx + (sealTarget_.x - boardCx) * eased,
                              boardCy + (sealTarget_.y - boardCy) * eased};
            amberVignetteAlpha_ = 0.23f + eased * 0.18f;
            for (int i = 0; i < 5; ++i) {
                const float maxR = 55.0f + static_cast<float>(i) * 52.0f;
                ringRadii_[i] = maxR * (1.0f - eased * 0.92f);
                ringAlphas_[i] = 0.78f * (1.0f - eased * 0.5f);
            }
            if (static_cast<int>(wElapsed * 50.0f) % 2 == 0)
                spawnWp(sealTarget_, 45.0f - eased * 30.0f, 10.0f, 45.0f);
        } else if (wElapsed < kSealEnd) {
            // Phase 3: Seal flash — undo count destroyed
            const float sp = (wElapsed - kConvergeEnd) / (kSealEnd - kConvergeEnd);
            mandalaScale_ = 0.45f - sp * 0.45f;
            mandalaAlpha_ = 0.37f * (1.0f - sp);
            mandalaRotation_ += 0.25f * (1.0f / 60.0f);
            amberVignetteAlpha_ = 0.41f * (1.0f - sp * 0.8f);
            if (!worldSealTriggered_) {
                worldSealTriggered_ = true;
                remainingUndos_ = 0;
                shakeIntensity_ = 5.5f;
                for (int i = 0; i < 40; ++i) spawnWp(sealTarget_, 6.0f, 35.0f, 140.0f);
            }
            sealFlashAlpha_ = std::sin(sp * 3.14159265f) * 0.9f;
            ringRadii_ = {};
            ringAlphas_ = {};
            shakeIntensity_ = std::max(0.0f, shakeIntensity_ * 0.88f - 0.15f);
        } else if (wElapsed < kDissipateEnd) {
            // Phase 4: Dissipate — all visuals fade
            const float dp = (wElapsed - kSealEnd) / (kDissipateEnd - kSealEnd);
            mandalaAlpha_ = 0.37f * (1.0f - dp);
            amberVignetteAlpha_ = 0.41f * (1.0f - dp);
            sealFlashAlpha_ = 0.9f * (1.0f - dp) * (1.0f - dp);
            mandalaRotation_ += 0.12f * (1.0f / 60.0f);
            shakeIntensity_ = 0.0f;
        } else {
            // Cleanup
            worldAnimPending_ = false;
            mandalaAlpha_ = 0.0f;
            amberVignetteAlpha_ = 0.0f;
            sealFlashAlpha_ = 0.0f;
            ringRadii_ = {};
            ringAlphas_ = {};
            worldParticles_.clear();
            worldSealTriggered_ = false;
            shakeIntensity_ = 0.0f;
        }

        // Tick all world particles
        constexpr float kDt = 1.0f / 60.0f;
        for (auto& wp : worldParticles_) {
            wp.life += kDt;
            wp.pos += wp.vel * kDt;
            wp.vel.x *= 0.975f;
            wp.vel.y *= 0.975f;
            wp.rotation += wp.rotSpeed * kDt;
        }
        worldParticles_.erase(
            std::remove_if(worldParticles_.begin(), worldParticles_.end(),
                [](const WorldParticle& wp) { return wp.life >= wp.maxLife; }),
            worldParticles_.end());
    }

    // Strength card animation: Convergence → Infusion → Settle
    if (strengthAnimPending_) {
        const float sElapsed = strengthAnimClock_.getElapsedTime().asSeconds();
        constexpr float kConvergeEndS = 0.25f;
        constexpr float kInfuseEndS = 0.55f;
        constexpr float kSettleEndS = 1.0f;
        static thread_local std::mt19937 sRng(std::random_device{}());

        const sf::Vector2f piecePx = board_.cellToPixel(strengthProtectedPos_.x, strengthProtectedPos_.y);

        auto spawnSp = [&](sf::Vector2f origin, float spread, float spdMin, float spdMax, bool inward) {
            const float a = std::uniform_real_distribution<float>(0.0f, 2.0f * 3.14159265f)(sRng);
            const float s = std::uniform_real_distribution<float>(spdMin, spdMax)(sRng);
            StrengthParticle sp;
            sp.pos = origin + sf::Vector2f{std::uniform_real_distribution<float>(-spread, spread)(sRng),
                                           std::uniform_real_distribution<float>(-spread, spread)(sRng)};
            if (inward) {
                const sf::Vector2f dir = piecePx - sp.pos;
                const float d = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                sp.vel = (d > 1.0f) ? (dir / d) * s : sf::Vector2f{std::cos(a) * s, std::sin(a) * s};
            } else {
                sp.vel = {std::cos(a) * s, std::sin(a) * s};
            }
            sp.life = 0.0f;
            sp.maxLife = std::uniform_real_distribution<float>(0.3f, 1.0f)(sRng);
            sp.scale = std::uniform_real_distribution<float>(0.6f, 1.6f)(sRng);
            strengthParticles_.push_back(sp);
        };

        if (sElapsed < kConvergeEndS) {
            // Phase 0: Golden particles rush inward toward the empowered piece
            constexpr float kSpawnRadius = 80.0f;
            if (static_cast<int>(sElapsed * 120.0f) % 2 == 0) {
                for (int i = 0; i < 3; ++i)
                    spawnSp(piecePx, kSpawnRadius, 35.0f, 90.0f, true);
            }
        } else if (sElapsed < kInfuseEndS) {
            // Phase 1: Golden flash + shockwave ring expands outward
            const float ip = (sElapsed - kConvergeEndS) / (kInfuseEndS - kConvergeEndS);
            const float easedIp = 1.0f - (1.0f - ip) * (1.0f - ip);
            strengthFlashAlpha_ = (1.0f - ip) * 0.85f;
            strengthShockwaveRadius_ = 14.0f + easedIp * 60.0f;
            strengthShockwaveAlpha_ = (1.0f - ip * 0.6f) * 0.75f;
            // Spawn trailing sparks along shockwave
            if (static_cast<int>(sElapsed * 90.0f) % 3 == 0) {
                for (int i = 0; i < 2; ++i)
                    spawnSp(piecePx, strengthShockwaveRadius_ * 0.5f, 15.0f, 55.0f, false);
            }
        } else if (sElapsed < kSettleEndS) {
            // Phase 2: Shockwave contracts, rune fades, particles dissipate outward
            const float sp = (sElapsed - kInfuseEndS) / (kSettleEndS - kInfuseEndS);
            const float easedSp = 1.0f - (1.0f - sp) * (1.0f - sp);
            strengthFlashAlpha_ = 0.0f;
            strengthShockwaveRadius_ = 74.0f - easedSp * 50.0f;
            strengthShockwaveAlpha_ = 0.75f * (1.0f - easedSp);
        } else {
            // Cleanup
            strengthAnimPending_ = false;
            strengthFlashAlpha_ = 0.0f;
            strengthShockwaveRadius_ = 0.0f;
            strengthShockwaveAlpha_ = 0.0f;
            strengthParticles_.clear();
        }

        // Tick all strength particles
        constexpr float kDtS = 1.0f / 60.0f;
        for (auto& sp : strengthParticles_) {
            sp.life += kDtS;
            sp.pos += sp.vel * kDtS;
            sp.vel.x *= 0.96f;
            sp.vel.y *= 0.96f;
        }
        strengthParticles_.erase(
            std::remove_if(strengthParticles_.begin(), strengthParticles_.end(),
                [](const StrengthParticle& sp) { return sp.life >= sp.maxLife; }),
            strengthParticles_.end());
    }

    // Empress card animation: Rooting → Blooming → Scatter
    if (empressAnimPending_) {
        const float eElapsed = empressAnimClock_.getElapsedTime().asSeconds();
        constexpr float kRootEndE = 0.45f;
        constexpr float kBloomEndE = 0.95f;
        constexpr float kScatterEndE = 1.5f;
        static thread_local std::mt19937 eRng(std::random_device{}());

        const sf::Vector2f srcPx = board_.cellToPixel(empressSourcePiece_.x, empressSourcePiece_.y);
        const sf::Vector2f tgtPx = board_.cellToPixel(empressTargetCell_.x, empressTargetCell_.y);

        auto spawnPetal = [&](sf::Vector2f origin, float spread, float spdMin, float spdMax, bool outward) {
            EmpressPetal p;
            p.pos = origin + sf::Vector2f{std::uniform_real_distribution<float>(-spread, spread)(eRng),
                                          std::uniform_real_distribution<float>(-spread, spread)(eRng)};
            const float a = std::uniform_real_distribution<float>(0.0f, 2.0f * 3.14159265f)(eRng);
            const float s = std::uniform_real_distribution<float>(spdMin, spdMax)(eRng);
            if (outward) {
                const sf::Vector2f dir = p.pos - tgtPx;
                const float d = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                p.vel = (d > 1.0f) ? (dir / d) * s : sf::Vector2f{std::cos(a) * s, std::sin(a) * s};
            } else {
                p.vel = {std::cos(a) * s, std::sin(a) * s};
            }
            p.life = 0.0f;
            p.maxLife = std::uniform_real_distribution<float>(0.6f, 1.8f)(eRng);
            p.rotation = std::uniform_real_distribution<float>(0.0f, 360.0f)(eRng);
            p.rotSpeed = std::uniform_real_distribution<float>(-80.0f, 80.0f)(eRng);
            p.scale = std::uniform_real_distribution<float>(0.5f, 1.3f)(eRng);
            p.swayPhase = std::uniform_real_distribution<float>(0.0f, 2.0f * 3.14159265f)(eRng);
            p.swayAmp = std::uniform_real_distribution<float>(0.3f, 1.2f)(eRng);
            p.colorType = std::uniform_int_distribution<int>(0, 2)(eRng);
            empressPetals_.push_back(p);
        };

        auto spawnFirefly = [&](sf::Vector2f origin, float spread) {
            EmpressFirefly ff;
            ff.pos = origin + sf::Vector2f{std::uniform_real_distribution<float>(-spread, spread)(eRng),
                                           std::uniform_real_distribution<float>(-spread, spread)(eRng)};
            const float a = std::uniform_real_distribution<float>(0.0f, 2.0f * 3.14159265f)(eRng);
            const float s = std::uniform_real_distribution<float>(6.0f, 22.0f)(eRng);
            ff.vel = {std::cos(a) * s, std::sin(a) * s - std::uniform_real_distribution<float>(4.0f, 12.0f)(eRng)};
            ff.life = 0.0f;
            ff.maxLife = std::uniform_real_distribution<float>(0.8f, 1.6f)(eRng);
            ff.glowPhase = std::uniform_real_distribution<float>(0.0f, 2.0f * 3.14159265f)(eRng);
            ff.baseRadius = std::uniform_real_distribution<float>(2.5f, 5.0f)(eRng);
            ff.orbitAngle = std::uniform_real_distribution<float>(0.0f, 2.0f * 3.14159265f)(eRng);
            ff.orbitSpeed = std::uniform_real_distribution<float>(1.0f, 3.0f)(eRng);
            empressFireflies_.push_back(ff);
        };

        if (eElapsed < kRootEndE) {
            // Phase 0: Vine grows from source piece toward target cell
            empressVineProgress_ = eElapsed / kRootEndE;
            empressBloomPhase_ = 0.0f;
            // Spawn leaf buds at intervals along vine
            if (static_cast<int>(eElapsed * 60.0f) % 5 == 0) {
                const float t = std::uniform_real_distribution<float>(0.0f, empressVineProgress_)(eRng);
                const sf::Vector2f vp = {srcPx.x + (tgtPx.x - srcPx.x) * t,
                                         srcPx.y + (tgtPx.y - srcPx.y) * t};
                spawnPetal(vp, 6.0f, 2.0f, 8.0f, false);
            }
        } else if (eElapsed < kBloomEndE) {
            // Phase 1: Flower blooms at target cell
            empressVineProgress_ = 1.0f;
            empressBloomPhase_ = (eElapsed - kRootEndE) / (kBloomEndE - kRootEndE);
            // Place the piece mid-bloom
            if (!empressPiecePlaced_ && empressBloomPhase_ >= 0.35f) {
                empressPiecePlaced_ = true;
                board_.placePieceAt(empressTargetCell_.x, empressTargetCell_.y, empressPlayerPiece_);
                winner_ = board_.winnerFromLastMove();
                if (winner_ != Board::Piece::None) gameOver_ = true;
            }
            // Spawn initial fireflies at bloom center
            if (empressBloomPhase_ > 0.5f && static_cast<int>(eElapsed * 50.0f) % 4 == 0)
                spawnFirefly(tgtPx, 10.0f);
        } else if (eElapsed < kScatterEndE) {
            // Phase 2: Petals scatter outward, fireflies drift
            empressBloomPhase_ = 1.0f;
            const float sp = (eElapsed - kBloomEndE) / (kScatterEndE - kBloomEndE);
            // Burst petals at start of scatter
            if (sp < 0.15f && static_cast<int>(eElapsed * 90.0f) % 3 == 0)
                spawnPetal(tgtPx, 8.0f, 12.0f, 50.0f, true);
            // Fireflies continue
            if (static_cast<int>(eElapsed * 38.0f) % 5 == 0)
                spawnFirefly(tgtPx, 18.0f);
        } else {
            // Cleanup
            empressAnimPending_ = false;
            empressPetals_.clear();
            empressFireflies_.clear();
        }

        // Tick all petals
        constexpr float kDtE = 1.0f / 60.0f;
        for (auto& p : empressPetals_) {
            p.life += kDtE;
            p.pos += p.vel * kDtE;
            p.vel.x *= 0.985f;
            p.vel.y *= 0.985f;
            p.vel.x += std::sin(p.swayPhase + eElapsed * 2.5f) * p.swayAmp * 0.15f;
            p.rotation += p.rotSpeed * kDtE;
        }
        empressPetals_.erase(
            std::remove_if(empressPetals_.begin(), empressPetals_.end(),
                [](const EmpressPetal& p) { return p.life >= p.maxLife; }),
            empressPetals_.end());

        // Tick all fireflies
        for (auto& ff : empressFireflies_) {
            ff.life += kDtE;
            ff.pos += ff.vel * kDtE;
            ff.vel.x *= 0.99f;
            ff.vel.y *= 0.99f;
            ff.vel.y -= 0.06f; // gentle upward drift
        }
        empressFireflies_.erase(
            std::remove_if(empressFireflies_.begin(), empressFireflies_.end(),
                [](const EmpressFirefly& ff) { return ff.life >= ff.maxLife; }),
            empressFireflies_.end());
    }

    // High Priestess card animation: Descent → Purification → Dissipate
    if (highPriestessAnimPending_) {
        const float hpElapsed = hpAnimClock_.getElapsedTime().asSeconds();
        constexpr float kDescentEndHP = 0.40f;
        constexpr float kPurifyEndHP = 0.85f;
        constexpr float kDissipateEndHP = 1.30f;
        static thread_local std::mt19937 hpRng(std::random_device{}());

        const sf::Vector2f centerPx = board_.cellToPixel(hpCrossCenter_.x, hpCrossCenter_.y);

        auto spawnMote = [&](sf::Vector2f origin, float spread, float spdMin, float spdMax) {
            HpMote m;
            m.pos = origin + sf::Vector2f{std::uniform_real_distribution<float>(-spread, spread)(hpRng),
                                          std::uniform_real_distribution<float>(-spread, spread)(hpRng)};
            const float a = std::uniform_real_distribution<float>(0.0f, 2.0f * 3.14159265f)(hpRng);
            const float s = std::uniform_real_distribution<float>(spdMin, spdMax)(hpRng);
            m.vel = {std::cos(a) * s, std::sin(a) * s - std::uniform_real_distribution<float>(8.0f, 24.0f)(hpRng)};
            m.life = 0.0f;
            m.maxLife = std::uniform_real_distribution<float>(0.5f, 1.4f)(hpRng);
            m.scale = std::uniform_real_distribution<float>(0.5f, 1.3f)(hpRng);
            hpMotes_.push_back(m);
        };

        if (hpElapsed < kDescentEndHP) {
            // Phase 0: Pillars descend from above
            const float dp = hpElapsed / kDescentEndHP;
            const float eased = 1.0f - (1.0f - dp) * (1.0f - dp);
            hpPillarAlpha_ = dp * 0.78f;
            hpPillarYOffset_ = -120.0f + eased * 130.0f;
            hpBeamProgress_ = 0.0f;
            // Gentle mote emission from pillar bases
            if (dp > 0.75f && static_cast<int>(hpElapsed * 40.0f) % 5 == 0)
                spawnMote({centerPx.x - 18.0f, centerPx.y}, 4.0f, 3.0f, 10.0f);
        } else if (hpElapsed < kPurifyEndHP) {
            // Phase 1: Cross beams burst, pieces dissolve
            const float pp = (hpElapsed - kDescentEndHP) / (kPurifyEndHP - kDescentEndHP);
            hpPillarAlpha_ = 0.78f + pp * 0.22f;
            hpPillarYOffset_ = 10.0f + std::sin(pp * 3.14159265f * 2.0f) * 3.0f;
            hpBeamProgress_ = pp;
            hpCrescentAlpha_ = 0.0f;

            // Remove pieces at ~30% progress
            if (!hpPiecesRemoved_ && pp >= 0.30f) {
                hpPiecesRemoved_ = true;
                for (int i = 0; i < Board::kBoardSize; ++i) {
                    board_.removePieceAt(hpCrossCenter_.x, i);
                    board_.removePieceAt(i, hpCrossCenter_.y);
                }
                winner_ = Board::Piece::None;
                gameOver_ = false;
            }

            // Spawn motes from dissolving pieces along the cross (full board edge to edge)
            if (pp > 0.25f && static_cast<int>(hpElapsed * 55.0f) % 3 == 0) {
                const float t = std::uniform_real_distribution<float>(0.0f, 1.0f)(hpRng);
                const float bL = board_.cellToPixel(0, 0).x;
                const float bR = board_.cellToPixel(0, Board::kBoardSize - 1).x;
                const float bT = board_.cellToPixel(0, 0).y;
                const float bB = board_.cellToPixel(Board::kBoardSize - 1, 0).y;
                if (std::uniform_int_distribution<int>(0, 1)(hpRng) == 0) {
                    spawnMote({bL + (bR - bL) * t, centerPx.y}, 4.0f, 5.0f, 18.0f);
                } else {
                    spawnMote({centerPx.x, bT + (bB - bT) * t}, 4.0f, 5.0f, 18.0f);
                }
            }
        } else if (hpElapsed < kDissipateEndHP) {
            // Phase 2: Pillars fade, beams contract, crescent appears
            const float dp2 = (hpElapsed - kPurifyEndHP) / (kDissipateEndHP - kPurifyEndHP);
            hpPillarAlpha_ = 1.0f * (1.0f - dp2);
            hpPillarYOffset_ = 10.0f - dp2 * 140.0f;
            hpBeamProgress_ = 1.0f - dp2 * 0.6f;
            // Crescent fades in then out
            if (dp2 < 0.25f) hpCrescentAlpha_ = dp2 / 0.25f * 0.75f;
            else hpCrescentAlpha_ = 0.75f * (1.0f - (dp2 - 0.25f) / 0.75f);
            // Residual motes
            if (dp2 < 0.4f && static_cast<int>(hpElapsed * 30.0f) % 6 == 0)
                spawnMote(centerPx, 12.0f, 2.0f, 8.0f);
        } else {
            // Cleanup
            highPriestessAnimPending_ = false;
            hpPillarAlpha_ = 0.0f;
            hpPillarYOffset_ = -120.0f;
            hpBeamProgress_ = 0.0f;
            hpCrescentAlpha_ = 0.0f;
            hpMotes_.clear();
            hpDissolvingPieces_.clear();
        }

        // Tick dissolving pieces
        constexpr float kDtHP = 1.0f / 60.0f;
        for (auto& dp : hpDissolvingPieces_) {
            dp.dissolveProgress += kDtHP / 0.4f; // 0.4s dissolve duration
        }

        // Tick motes
        for (auto& m : hpMotes_) {
            m.life += kDtHP;
            m.pos += m.vel * kDtHP;
            m.vel.x *= 0.985f;
            m.vel.y *= 0.985f;
        }
        hpMotes_.erase(
            std::remove_if(hpMotes_.begin(), hpMotes_.end(),
                [](const HpMote& m) { return m.life >= m.maxLife; }),
            hpMotes_.end());
    }

    // Sun card animation: Rise → Illuminate → Fade
    if (sunAnimPending_) {
        const float sElapsed = sunAnimClock_.getElapsedTime().asSeconds();
        constexpr float kRiseEndS = 0.40f;
        constexpr float kIlluminateEndS = 0.85f;
        constexpr float kFadeEndS = 1.20f;
        static thread_local std::mt19937 sRng(std::random_device{}());

        const sf::Vector2f tengenPx = board_.cellToPixel(7, 7);
        const sf::Vector2f zoneTopLeft = board_.cellToPixel(5, 5);
        const sf::Vector2f zoneBotRight = board_.cellToPixel(9, 9);
        const float zoneHalfW = (zoneBotRight.x - zoneTopLeft.x) * 0.5f + 18.0f;

        auto spawnSunMote = [&](sf::Vector2f origin, float spread, float spdMin, float spdMax) {
            SunMote m;
            m.pos = origin + sf::Vector2f{std::uniform_real_distribution<float>(-spread, spread)(sRng),
                                          std::uniform_real_distribution<float>(-spread, spread)(sRng)};
            const float a = std::uniform_real_distribution<float>(0.0f, 2.0f * 3.14159265f)(sRng);
            const float s = std::uniform_real_distribution<float>(spdMin, spdMax)(sRng);
            m.vel = {std::cos(a) * s, std::sin(a) * s};
            m.life = 0.0f;
            m.maxLife = std::uniform_real_distribution<float>(0.5f, 1.3f)(sRng);
            m.scale = std::uniform_real_distribution<float>(0.5f, 1.4f)(sRng);
            sunMotes_.push_back(m);
        };

        if (sElapsed < kRiseEndS) {
            // Phase 0: Sun disc rises from above, scales up, rays emerge
            const float rp = sElapsed / kRiseEndS;
            const float eased = 1.0f - (1.0f - rp) * (1.0f - rp);
            sunDiscScale_ = 0.3f + eased * 0.7f;
            sunDiscAlpha_ = rp * 0.85f;
            sunDiscYOffset_ = -160.0f + eased * 170.0f;
            sunRotation_ += 0.5f * (1.0f / 60.0f);
            sunRayAlpha_ = rp > 0.5f ? (rp - 0.5f) / 0.5f * 0.7f : 0.0f;
            sunZoneGlowAlpha_ = rp * 0.1f;
            sunShockwaveAlpha_ = 0.0f;
            if (rp > 0.7f && static_cast<int>(sElapsed * 40.0f) % 6 == 0)
                spawnSunMote(tengenPx, 30.0f, 5.0f, 20.0f);
        } else if (sElapsed < kIlluminateEndS) {
            // Phase 1: Sun pulses, shockwave expands to 5x5 boundary
            const float ip = (sElapsed - kRiseEndS) / (kIlluminateEndS - kRiseEndS);
            const float easedIp = 1.0f - (1.0f - ip) * (1.0f - ip);
            sunDiscScale_ = 1.0f + std::sin(ip * 3.14159265f * 2.5f) * 0.06f;
            sunDiscAlpha_ = 0.85f + std::sin(ip * 3.14159265f * 2.5f) * 0.1f;
            sunDiscYOffset_ = 10.0f + std::sin(ip * 3.14159265f * 1.5f) * 4.0f;
            sunRotation_ += 0.8f * (1.0f / 60.0f);
            sunRayAlpha_ = 0.7f + std::sin(ip * 3.14159265f * 2.0f) * 0.2f;
            sunZoneGlowAlpha_ = 0.10f + easedIp * 0.12f;
            sunShockwaveRadius_ = 10.0f + easedIp * zoneHalfW;
            sunShockwaveAlpha_ = 0.65f * (1.0f - ip * 0.5f);
            // Motes burst from zone
            if (static_cast<int>(sElapsed * 60.0f) % 2 == 0)
                spawnSunMote(tengenPx, zoneHalfW * 0.8f, 8.0f, 35.0f);
        } else if (sElapsed < kFadeEndS) {
            // Phase 2: Sun rises back, fades, everything dissipates
            const float fp = (sElapsed - kIlluminateEndS) / (kFadeEndS - kIlluminateEndS);
            const float easedFp = 1.0f - (1.0f - fp) * (1.0f - fp);
            sunDiscScale_ = 1.0f - easedFp * 0.6f;
            sunDiscAlpha_ = 0.95f * (1.0f - easedFp);
            sunDiscYOffset_ = 10.0f - easedFp * 140.0f;
            sunRotation_ += 0.3f * (1.0f / 60.0f);
            sunRayAlpha_ = 0.9f * (1.0f - easedFp);
            sunZoneGlowAlpha_ = 0.22f * (1.0f - easedFp);
            sunShockwaveRadius_ = zoneHalfW - easedFp * 30.0f;
            sunShockwaveAlpha_ = 0.65f * (1.0f - easedFp);
            if (fp < 0.4f && static_cast<int>(sElapsed * 30.0f) % 5 == 0)
                spawnSunMote(tengenPx, 20.0f, 3.0f, 12.0f);
        } else {
            // Cleanup
            sunAnimPending_ = false;
            sunDiscAlpha_ = 0.0f;
            sunRayAlpha_ = 0.0f;
            sunZoneGlowAlpha_ = 0.0f;
            sunShockwaveAlpha_ = 0.0f;
            sunMotes_.clear();
        }

        // Tick motes
        constexpr float kDtSun = 1.0f / 60.0f;
        for (auto& m : sunMotes_) {
            m.life += kDtSun;
            m.pos += m.vel * kDtSun;
            m.vel.x *= 0.98f;
            m.vel.y *= 0.98f;
        }
        sunMotes_.erase(
            std::remove_if(sunMotes_.begin(), sunMotes_.end(),
                [](const SunMote& m) { return m.life >= m.maxLife; }),
            sunMotes_.end());
    }

    // Star card animation: Descent → Bloom → Persistent idle
    if (starAnimPending_) {
        const float stElapsed = starAnimClock_.getElapsedTime().asSeconds();
        constexpr float kDescentEndSt = 0.35f;
        constexpr float kBloomEndSt = 0.70f;
        static thread_local std::mt19937 stRng(std::random_device{}());

        const sf::Vector2f starPx = board_.cellToPixel(starHighlightPos_.x, starHighlightPos_.y);

        auto spawnStarDust = [&](sf::Vector2f origin, float spread, float spdMin, float spdMax) {
            StarDust sd;
            sd.pos = origin + sf::Vector2f{std::uniform_real_distribution<float>(-spread, spread)(stRng),
                                           std::uniform_real_distribution<float>(-spread, spread)(stRng)};
            const float a = std::uniform_real_distribution<float>(0.0f, 2.0f * 3.14159265f)(stRng);
            const float s = std::uniform_real_distribution<float>(spdMin, spdMax)(stRng);
            sd.vel = {std::cos(a) * s, std::sin(a) * s};
            sd.life = 0.0f;
            sd.maxLife = std::uniform_real_distribution<float>(0.4f, 0.9f)(stRng);
            sd.scale = std::uniform_real_distribution<float>(0.4f, 1.0f)(stRng);
            sd.rotation = std::uniform_real_distribution<float>(0.0f, 360.0f)(stRng);
            sd.rotSpeed = std::uniform_real_distribution<float>(-100.0f, 100.0f)(stRng);
            starDustParticles_.push_back(sd);
        };

        if (!starInPersistentMode_ && stElapsed < kDescentEndSt) {
            // Phase 0: Star descends from above
            const float dp = stElapsed / kDescentEndSt;
            const float eased = 1.0f - (1.0f - dp) * (1.0f - dp);
            starYOffset_ = -60.0f + eased * 70.0f;
            starScale_ = 0.3f + eased * 0.7f;
            starAlpha_ = dp * 0.85f;
            starRotation_ += 1.5f * (1.0f / 60.0f);
            starRayAlpha_ = 0.0f;
            // Stardust trail
            if (static_cast<int>(stElapsed * 50.0f) % 3 == 0)
                spawnStarDust({starPx.x, starPx.y + starYOffset_}, 5.0f, 2.0f, 10.0f);
        } else if (!starInPersistentMode_ && stElapsed < kBloomEndSt) {
            // Phase 1: Star blooms — rays flash, particles burst
            const float bp = (stElapsed - kDescentEndSt) / (kBloomEndSt - kDescentEndSt);
            starYOffset_ = 10.0f + std::sin(bp * 3.14159265f * 2.0f) * 3.0f;
            starScale_ = 1.0f + std::sin(bp * 3.14159265f * 2.0f) * 0.1f;
            starAlpha_ = 0.85f + std::sin(bp * 3.14159265f * 2.0f) * 0.1f;
            starRotation_ += 0.6f * (1.0f / 60.0f);
            // Rays flash then fade
            if (bp < 0.3f) starRayAlpha_ = bp / 0.3f * 0.7f;
            else starRayAlpha_ = 0.7f * (1.0f - (bp - 0.3f) / 0.7f);
            // Particle burst
            if (bp < 0.25f && static_cast<int>(stElapsed * 80.0f) % 2 == 0)
                spawnStarDust(starPx, 6.0f, 15.0f, 50.0f);
        } else {
            // Phase 2: Persistent idle — gentle pulse, slow rotation
            if (!starInPersistentMode_) {
                starInPersistentMode_ = true;
                starYOffset_ = 0.0f;
                starScale_ = 1.0f;
                starAlpha_ = 0.75f;
                starRayAlpha_ = 0.0f;
            }
            starRotation_ += 0.35f * (1.0f / 60.0f);
            // Subtle breathing pulse
            const float pulse = 0.5f + 0.5f * std::sin(stElapsed * 1.8f);
            starScale_ = 1.0f + pulse * 0.06f;
            starAlpha_ = 0.65f + pulse * 0.15f;
            // Occasional sparkle
            if (static_cast<int>(stElapsed * 25.0f) % 13 == 0)
                spawnStarDust(starPx, 10.0f, 3.0f, 12.0f);
        }

        // Clear animation when highlight is invalidated
        if (!starHighlightValid_) {
            starAnimPending_ = false;
            starInPersistentMode_ = false;
            starDustParticles_.clear();
        }

        // Tick stardust
        constexpr float kDtSt = 1.0f / 60.0f;
        for (auto& sd : starDustParticles_) {
            sd.life += kDtSt;
            sd.pos += sd.vel * kDtSt;
            sd.vel.x *= 0.985f;
            sd.vel.y *= 0.985f;
            sd.rotation += sd.rotSpeed * kDtSt;
        }
        starDustParticles_.erase(
            std::remove_if(starDustParticles_.begin(), starDustParticles_.end(),
                [](const StarDust& sd) { return sd.life >= sd.maxLife; }),
            starDustParticles_.end());
    }

    // Moon card animation (Lunar Mist)
    if (moonAnimPending_) {
        const float mElapsed = moonAnimClock_.getElapsedTime().asSeconds();
        constexpr float kMoonriseEnd = 0.40f;
        constexpr float kMistEnd = 0.90f;
        constexpr float kFadeEnd = 1.30f;
        static thread_local std::mt19937 mRng(std::random_device{}());

        if (mElapsed < kMoonriseEnd) {
            // Phase 0: Moonrise — moon fades in and rises
            const float t = mElapsed / kMoonriseEnd;
            moonAlpha_ = t;
            moonYOffset_ = -40.0f + t * 40.0f;  // rise from -40 to 0
            moonMistAlpha_ = 0.0f;
            moonOverlayAlpha_ = 0.0f;
        } else if (mElapsed < kMistEnd) {
            // Phase 1: Mist Veil — moon fully visible, mist sweeps across board
            const float t = (mElapsed - kMoonriseEnd) / (kMistEnd - kMoonriseEnd);
            moonAlpha_ = 1.0f;
            moonYOffset_ = 0.0f;
            moonMistAlpha_ = t;
            moonOverlayAlpha_ = t * 0.6f;
            // Spawn mist particles sweeping across the board area
            if (moonMistParticles_.size() < 80 && t > 0.05f) {
                const float boardLeft = board_.cellToPixel(0, 0).x - 20.0f;
                const float boardRight = board_.cellToPixel(0, Board::kBoardSize - 1).x + 20.0f;
                const float boardTop = board_.cellToPixel(0, 0).y - 20.0f;
                const float boardBottom = board_.cellToPixel(Board::kBoardSize - 1, 0).y + 20.0f;
                for (int i = 0; i < 3; ++i) {
                    MoonMistParticle mp;
                    mp.pos = {static_cast<float>(boardLeft + std::uniform_real_distribution<>(0.0, boardRight - boardLeft)(mRng)),
                              static_cast<float>(boardTop + std::uniform_real_distribution<>(0.0, boardBottom - boardTop)(mRng))};
                    mp.vel = {static_cast<float>(std::uniform_real_distribution<>(15.0, 40.0)(mRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-8.0, 8.0)(mRng))};
                    mp.life = 0.0f;
                    mp.maxLife = static_cast<float>(std::uniform_real_distribution<>(2.0, 4.5)(mRng));
                    mp.scale = static_cast<float>(std::uniform_real_distribution<>(0.7, 2.5)(mRng));
                    moonMistParticles_.push_back(mp);
                }
            }
        } else if (mElapsed < kFadeEnd) {
            // Phase 2: Dissipate — moon fades, mist clears
            const float t = (mElapsed - kMistEnd) / (kFadeEnd - kMistEnd);
            moonAlpha_ = 1.0f - t;
            moonYOffset_ = t * 25.0f;  // sink slightly
            moonMistAlpha_ = 1.0f - t;
            moonOverlayAlpha_ = (1.0f - t) * 0.6f;
        } else {
            moonAnimPending_ = false;
            moonAlpha_ = 0.0f;
            moonYOffset_ = 0.0f;
            moonMistAlpha_ = 0.0f;
            moonOverlayAlpha_ = 0.0f;
            moonMistParticles_.clear();
        }

        // Tick mist particles
        const float boardLeft = board_.cellToPixel(0, 0).x - 20.0f;
        const float boardRight = board_.cellToPixel(0, Board::kBoardSize - 1).x + 20.0f;
        const float boardTop = board_.cellToPixel(0, 0).y - 20.0f;
        const float boardBottom = board_.cellToPixel(Board::kBoardSize - 1, 0).y + 20.0f;
        for (auto& mp : moonMistParticles_) {
            mp.life += 1.0f / 60.0f;
            mp.pos += mp.vel * (1.0f / 60.0f);
            // Wrap horizontally
            if (mp.pos.x > boardRight + 10.0f) mp.pos.x = boardLeft - 10.0f;
            if (mp.pos.x < boardLeft - 10.0f) mp.pos.x = boardRight + 10.0f;
            // Bounce vertically within board area
            if (mp.pos.y > boardBottom) { mp.pos.y = boardBottom; mp.vel.y *= -1.0f; }
            if (mp.pos.y < boardTop) { mp.pos.y = boardTop; mp.vel.y *= -1.0f; }
        }
        moonMistParticles_.erase(
            std::remove_if(moonMistParticles_.begin(), moonMistParticles_.end(),
                [](const MoonMistParticle& mp) { return mp.life >= mp.maxLife; }),
            moonMistParticles_.end());
    }

    // Moon original-position marker fade
    if (moonMarkerAlpha_ > 0.0f && moonOriginalPos_.x >= 0) {
        moonMarkerAlpha_ -= 1.0f / 120.0f;  // fade over ~2s at 60fps
        if (moonMarkerAlpha_ <= 0.0f) {
            moonMarkerAlpha_ = 0.0f;
            moonOriginalPos_ = {-1, -1};
        }
    }

    // Hermit card animation (Mirror-Still Water)
    if (hermitAnimPending_) {
        const float hElapsed = hermitAnimClock_.getElapsedTime().asSeconds();
        constexpr float kStillnessEnd = 0.50f;
        constexpr float kMirrorEnd = 1.00f;
        constexpr float kClarityEnd = 1.50f;
        const sf::Vector2f pondCenter = board_.cellToPixel(7, 7);
        static thread_local std::mt19937 hRng(std::random_device{}());

        if (hElapsed < kStillnessEnd) {
            // Phase 0: Stillness Descends
            const float t = hElapsed / kStillnessEnd;
            hermitOverlayAlpha_ = t * 0.55f;
            hermitPondRadius_ = 60.0f + t * 140.0f;  // pond expands
            hermitPondAlpha_ = t;
            hermitRingAlpha_ = t * 0.5f;
            // Spawn initial ripples
            if (hermitRipples_.empty() && t > 0.3f) {
                hermitRipples_.push_back({0.0f, 0.8f, 160.0f, 45.0f});
            }
            if (hermitRipples_.size() == 1 && t > 0.6f) {
                hermitRipples_.push_back({0.0f, 0.6f, 150.0f, 40.0f});
            }
        } else if (hElapsed < kMirrorEnd) {
            // Phase 1: Mirror Still Water
            const float t = (hElapsed - kStillnessEnd) / (kMirrorEnd - kStillnessEnd);
            hermitOverlayAlpha_ = 0.55f;
            hermitPondRadius_ = 200.0f;
            hermitPondAlpha_ = 1.0f;
            hermitRingAlpha_ = 0.5f + t * 0.5f;
            // Spawn lanterns
            if (hermitLanterns_.size() < 7 && t > 0.1f) {
                HermitLantern hl;
                hl.angle = std::uniform_real_distribution<>(0.0f, 6.2832f)(hRng);
                hl.orbitRadius = 130.0f + std::uniform_real_distribution<>(-30.0f, 30.0f)(hRng);
                hl.orbitSpeed = std::uniform_real_distribution<>(0.3f, 0.8f)(hRng);
                hl.life = 0.0f;
                hl.maxLife = 3.0f + std::uniform_real_distribution<>(0.0f, 2.0f)(hRng);
                hl.scale = std::uniform_real_distribution<>(0.6f, 1.4f)(hRng);
                hl.glowPhase = std::uniform_real_distribution<>(0.0f, 6.2832f)(hRng);
                hl.pos = {pondCenter.x + std::cos(hl.angle) * hl.orbitRadius,
                          pondCenter.y + std::sin(hl.angle) * hl.orbitRadius};
                hermitLanterns_.push_back(hl);
            }
            // Spawn periodic ripples
            if (static_cast<int>(hElapsed * 10.0f) % 3 == 0 && hermitRipples_.size() < 4) {
                hermitRipples_.push_back({0.0f, 0.5f + t * 0.3f, 140.0f + t * 40.0f, 38.0f + t * 10.0f});
            }
        } else if (hElapsed < kClarityEnd) {
            // Phase 2: Clarity
            const float t = (hElapsed - kMirrorEnd) / (kClarityEnd - kMirrorEnd);
            hermitOverlayAlpha_ = 0.55f * (1.0f - t);
            hermitPondAlpha_ = 1.0f - t;
            hermitRingAlpha_ = (1.0f - t) * (1.0f - t);
            hermitPondRadius_ = 200.0f + t * 30.0f;  // slight expansion while fading
        } else {
            hermitAnimPending_ = false;
            hermitOverlayAlpha_ = 0.0f;
            hermitPondAlpha_ = 0.0f;
            hermitRingAlpha_ = 0.0f;
            hermitPondRadius_ = 0.0f;
            hermitLanterns_.clear();
            hermitRipples_.clear();
        }

        // Tick ripples
        for (auto& r : hermitRipples_) {
            r.radius += r.speed * (1.0f / 60.0f);
            r.alpha -= 0.25f / 60.0f;
        }
        hermitRipples_.erase(
            std::remove_if(hermitRipples_.begin(), hermitRipples_.end(),
                [](const HermitRipple& r) { return r.radius >= r.maxRadius || r.alpha <= 0.0f; }),
            hermitRipples_.end());

        // Tick lanterns
        for (auto& hl : hermitLanterns_) {
            hl.life += 1.0f / 60.0f;
            hl.angle += hl.orbitSpeed * (1.0f / 60.0f);
            hl.pos = {pondCenter.x + std::cos(hl.angle) * hl.orbitRadius,
                      pondCenter.y + std::sin(hl.angle) * hl.orbitRadius * 0.7f};  // elliptical
        }
        hermitLanterns_.erase(
            std::remove_if(hermitLanterns_.begin(), hermitLanterns_.end(),
                [](const HermitLantern& hl) { return hl.life >= hl.maxLife; }),
            hermitLanterns_.end());
    }

    // Hierophant card animation (Rule Binding)
    if (hierophantAnimPending_) {
        const float hpElapsed = hierophantAnimClock_.getElapsedTime().asSeconds();
        constexpr float kLightEnd = 0.40f;
        constexpr float kBoundaryEnd = 0.90f;
        constexpr float kSealEnd = 1.30f;
        static thread_local std::mt19937 hpRng(std::random_device{}());

        if (hpElapsed < kLightEnd) {
            // Phase 0: Holy Light Descends
            const float t = hpElapsed / kLightEnd;
            hierophantLightAlpha_ = t;
            hierophantOuterDimAlpha_ = t * 0.35f;
            hierophantBoundaryAlpha_ = t * 0.4f;
            hierophantSealAlpha_ = 0.0f;
        } else if (hpElapsed < kBoundaryEnd) {
            // Phase 1: Sacred Boundary
            const float t = (hpElapsed - kLightEnd) / (kBoundaryEnd - kLightEnd);
            hierophantLightAlpha_ = 1.0f - t * 0.6f;
            hierophantOuterDimAlpha_ = 0.35f;
            hierophantBoundaryAlpha_ = 0.4f + t * 0.6f;
            hierophantSealAlpha_ = t * 0.7f;
            hierophantCornerAlpha_ = t;
            // Spawn golden particles within the 9x9 zone
            if (hierophantParticles_.size() < 30 && t > 0.1f) {
                const float zLeft = board_.cellToPixel(0, 3).x;
                const float zRight = board_.cellToPixel(0, 11).x;
                const float zTop = board_.cellToPixel(3, 0).y;
                const float zBottom = board_.cellToPixel(11, 0).y;
                for (int i = 0; i < 2; ++i) {
                    HierophantParticle hp;
                    hp.pos = {static_cast<float>(zLeft + std::uniform_real_distribution<>(0.0, zRight - zLeft)(hpRng)),
                              static_cast<float>(zTop + std::uniform_real_distribution<>(0.0, zBottom - zTop)(hpRng))};
                    hp.vel = {static_cast<float>(std::uniform_real_distribution<>(-15.0, 15.0)(hpRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-20.0, -5.0)(hpRng))};
                    hp.life = 0.0f;
                    hp.maxLife = static_cast<float>(std::uniform_real_distribution<>(1.5, 3.5)(hpRng));
                    hp.scale = static_cast<float>(std::uniform_real_distribution<>(0.5, 1.5)(hpRng));
                    hierophantParticles_.push_back(hp);
                }
            }
        } else if (hpElapsed < kSealEnd) {
            // Phase 2: Decree Sealed
            const float t = (hpElapsed - kBoundaryEnd) / (kSealEnd - kBoundaryEnd);
            hierophantLightAlpha_ = 0.4f * (1.0f - t);
            hierophantOuterDimAlpha_ = 0.35f * (1.0f - t);
            hierophantBoundaryAlpha_ = 1.0f - t * 0.6f;  // fade to subtle
            hierophantSealAlpha_ = 0.7f - t * 0.7f;      // cross fades
            hierophantCornerAlpha_ = 0.4f + 0.6f * (1.0f - t * 0.2f);  // persist strong
        } else {
            hierophantAnimPending_ = false;
            hierophantLightAlpha_ = 0.0f;
            hierophantBoundaryAlpha_ = 0.0f;
            hierophantSealAlpha_ = 0.0f;
            hierophantOuterDimAlpha_ = 0.0f;
            hierophantCornerAlpha_ = 0.7f;  // persistent corners stay
            hierophantParticles_.clear();
        }

        // Tick particles
        for (auto& hp : hierophantParticles_) {
            hp.life += 1.0f / 60.0f;
            hp.pos += hp.vel * (1.0f / 60.0f);
            hp.vel.y -= 2.0f * (1.0f / 60.0f);  // gentle upward drift
        }
        hierophantParticles_.erase(
            std::remove_if(hierophantParticles_.begin(), hierophantParticles_.end(),
                [](const HierophantParticle& hp) { return hp.life >= hp.maxLife; }),
            hierophantParticles_.end());
    }

    // Persistent corner markers: track hierophantRemaining_ for fade in/out
    if (!hierophantAnimPending_) {
        if (hierophantRemaining_ > 0 && hierophantCornerAlpha_ < 0.7f) {
            hierophantCornerAlpha_ += 0.8f / 60.0f;  // fade in
        } else if (hierophantRemaining_ == 0 && hierophantCornerAlpha_ > 0.0f) {
            hierophantCornerAlpha_ -= 0.6f / 60.0f;  // fade out when effect expires
            if (hierophantCornerAlpha_ < 0.0f) hierophantCornerAlpha_ = 0.0f;
        }
    }

    // Justice card animation (Heavenly Cycle)
    if (justiceAnimPending_) {
        const float jElapsed = justiceAnimClock_.getElapsedTime().asSeconds();
        constexpr float kJudgmentEnd = 0.40f;
        constexpr float kRetributionEnd = 0.80f;
        constexpr float kCycleEnd = 1.20f;
        static thread_local std::mt19937 jRng(std::random_device{}());

        if (jElapsed < kJudgmentEnd) {
            // Phase 0: Judgment Descends
            const float t = jElapsed / kJudgmentEnd;
            justiceScreenBrightAlpha_ = t * 0.25f;
            justiceScaleAlpha_ = t;
            justiceLightAlpha_ = t * 0.3f;
        } else if (jElapsed < kRetributionEnd) {
            // Phase 1: Karmic Retribution
            const float t = (jElapsed - kJudgmentEnd) / (kRetributionEnd - kJudgmentEnd);
            justiceScreenBrightAlpha_ = 0.25f;
            justiceScaleAlpha_ = 1.0f;
            justiceLightAlpha_ = 0.3f + t * 0.7f;
            justicePieceGlowAlpha_ = t;
            // Spawn feathers
            if (justiceFeathers_.size() < 30 && t > 0.1f) {
                for (int i = 0; i < 2; ++i) {
                    JusticeFeather jf;
                    jf.pos = {static_cast<float>(std::uniform_real_distribution<>(200.0, 1080.0)(jRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-40.0, 30.0)(jRng))};
                    jf.vel = {static_cast<float>(std::uniform_real_distribution<>(-15.0, 15.0)(jRng)),
                              static_cast<float>(std::uniform_real_distribution<>(20.0, 50.0)(jRng))};
                    jf.life = 0.0f;
                    jf.maxLife = static_cast<float>(std::uniform_real_distribution<>(2.5, 4.0)(jRng));
                    jf.scale = static_cast<float>(std::uniform_real_distribution<>(0.6, 1.5)(jRng));
                    jf.rotation = static_cast<float>(std::uniform_real_distribution<>(0.0, 6.2832)(jRng));
                    jf.swayPhase = static_cast<float>(std::uniform_real_distribution<>(0.0, 6.2832)(jRng));
                    jf.swayAmp = static_cast<float>(std::uniform_real_distribution<>(0.3, 1.2)(jRng));
                    justiceFeathers_.push_back(jf);
                }
            }
            // Remove pieces at 50% through phase 1
            if (!justicePiecesRemoved_ && t > 0.5f) {
                justicePiecesRemoved_ = true;
                if (justiceBlackPiece_.x >= 0) board_.removePieceAt(justiceBlackPiece_.x, justiceBlackPiece_.y);
                if (justiceWhitePiece_.x >= 0) board_.removePieceAt(justiceWhitePiece_.x, justiceWhitePiece_.y);
            }
        } else if (jElapsed < kCycleEnd) {
            // Phase 2: Cycle Complete
            const float t = (jElapsed - kRetributionEnd) / (kCycleEnd - kRetributionEnd);
            justiceScreenBrightAlpha_ = 0.25f * (1.0f - t);
            justiceScaleAlpha_ = 1.0f - t;
            justiceLightAlpha_ = 1.0f - t;
            justicePieceGlowAlpha_ = 1.0f - t;
        } else {
            justiceAnimPending_ = false;
            justiceScreenBrightAlpha_ = 0.0f;
            justiceScaleAlpha_ = 0.0f;
            justiceLightAlpha_ = 0.0f;
            justicePieceGlowAlpha_ = 0.0f;
            justiceFeathers_.clear();
            justiceBlackPiece_ = {-1, -1};
            justiceWhitePiece_ = {-1, -1};
        }

        // Tick feathers
        for (auto& jf : justiceFeathers_) {
            jf.life += 1.0f / 60.0f;
            jf.pos += jf.vel * (1.0f / 60.0f);
            jf.pos.x += std::sin(jf.swayPhase + jElapsed * 2.0f) * jf.swayAmp * (1.0f / 60.0f) * 10.0f;
            jf.rotation += (1.0f / 60.0f) * 0.3f;
        }
        justiceFeathers_.erase(
            std::remove_if(justiceFeathers_.begin(), justiceFeathers_.end(),
                [](const JusticeFeather& jf) { return jf.life >= jf.maxLife; }),
            justiceFeathers_.end());
    }

    // Hanged Man card animation (Self-Sacrifice)
    if (hangedManAnimPending_) {
        const float hmElapsed = hangedManAnimClock_.getElapsedTime().asSeconds();
        constexpr float kInversionEnd = 0.35f;
        constexpr float kSacrificeEnd = 0.75f;
        constexpr float kRetributionEnd = 1.15f;
        static thread_local std::mt19937 hmRng(std::random_device{}());

        if (hmElapsed < kInversionEnd) {
            // Phase 0: Inversion
            const float t = hmElapsed / kInversionEnd;
            hangedManOverlayAlpha_ = t * 0.40f;
            hangedManSacrificeGlowAlpha_ = t;
            hangedManThreadProgress_ = 0.0f;
        } else if (hmElapsed < kSacrificeEnd) {
            // Phase 1: Sacrifice
            const float t = (hmElapsed - kInversionEnd) / (kSacrificeEnd - kInversionEnd);
            hangedManOverlayAlpha_ = 0.40f + t * 0.25f;
            hangedManSacrificeGlowAlpha_ = 1.0f;
            hangedManThreadProgress_ = t;
            hangedManTargetGlowAlpha_ = t;
            // Remove sacrificed piece at 35% through phase 1
            if (!hangedManSacrificed_ && t > 0.35f && hangedManSacrificePos_.x >= 0) {
                hangedManSacrificed_ = true;
                board_.removePieceAt(hangedManSacrificePos_.x, hangedManSacrificePos_.y);
            }
            // Spawn crimson particles from sacrifice point
            if (hangedManSacrificed_ && hangedManParticles_.size() < 40 && t > 0.4f) {
                const auto sacPx = board_.cellToPixel(hangedManSacrificePos_.x, hangedManSacrificePos_.y);
                for (int i = 0; i < 2; ++i) {
                    HangedManParticle hp;
                    hp.pos = {static_cast<float>(sacPx.x + std::uniform_real_distribution<>(-8.0, 8.0)(hmRng)),
                              static_cast<float>(sacPx.y + std::uniform_real_distribution<>(-8.0, 8.0)(hmRng))};
                    hp.vel = {static_cast<float>(std::uniform_real_distribution<>(-30.0, 30.0)(hmRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-60.0, -20.0)(hmRng))};
                    hp.life = 0.0f;
                    hp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.8, 2.0)(hmRng));
                    hp.scale = static_cast<float>(std::uniform_real_distribution<>(0.8, 2.5)(hmRng));
                    hangedManParticles_.push_back(hp);
                }
            }
        } else if (hmElapsed < kRetributionEnd) {
            // Phase 2: Retribution
            const float t = (hmElapsed - kSacrificeEnd) / (kRetributionEnd - kSacrificeEnd);
            hangedManOverlayAlpha_ = 0.65f * (1.0f - t);
            hangedManSacrificeGlowAlpha_ = 1.0f - t;
            hangedManThreadProgress_ = 1.0f;
            hangedManTargetGlowAlpha_ = 1.0f - t;
            hangedManMarkAlpha_ = (1.0f - t) * 0.8f;
            // Remove opponent pieces at 30% through phase 2
            if (!hangedManTargetsRemoved_ && t > 0.30f) {
                hangedManTargetsRemoved_ = true;
                if (hangedManTargetA_.x >= 0) board_.removePieceAt(hangedManTargetA_.x, hangedManTargetA_.y);
                if (hangedManTargetB_.x >= 0) board_.removePieceAt(hangedManTargetB_.x, hangedManTargetB_.y);
            }
            // Spawn particles from target positions
            if (hangedManTargetsRemoved_ && hangedManParticles_.size() < 70 && t > 0.35f) {
                const auto spawnAt = [&](sf::Vector2i gp) {
                    if (gp.x < 0) return;
                    const auto px = board_.cellToPixel(gp.x, gp.y);
                    for (int i = 0; i < 2; ++i) {
                        HangedManParticle hp;
                        hp.pos = {static_cast<float>(px.x + std::uniform_real_distribution<>(-6.0, 6.0)(hmRng)),
                                  static_cast<float>(px.y + std::uniform_real_distribution<>(-6.0, 6.0)(hmRng))};
                        hp.vel = {static_cast<float>(std::uniform_real_distribution<>(-40.0, 40.0)(hmRng)),
                                  static_cast<float>(std::uniform_real_distribution<>(-50.0, -10.0)(hmRng))};
                        hp.life = 0.0f;
                        hp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.6, 1.5)(hmRng));
                        hp.scale = static_cast<float>(std::uniform_real_distribution<>(0.5, 1.8)(hmRng));
                        hangedManParticles_.push_back(hp);
                    }
                };
                spawnAt(hangedManTargetA_);
                spawnAt(hangedManTargetB_);
            }
        } else {
            hangedManAnimPending_ = false;
            hangedManOverlayAlpha_ = 0.0f;
            hangedManSacrificeGlowAlpha_ = 0.0f;
            hangedManThreadProgress_ = 0.0f;
            hangedManTargetGlowAlpha_ = 0.0f;
            hangedManMarkAlpha_ = 0.0f;
            hangedManParticles_.clear();
            hangedManSacrificePos_ = {-1, -1};
            hangedManTargetA_ = {-1, -1};
            hangedManTargetB_ = {-1, -1};
        }

        // Tick particles
        for (auto& hp : hangedManParticles_) {
            hp.life += 1.0f / 60.0f;
            hp.pos += hp.vel * (1.0f / 60.0f);
        }
        hangedManParticles_.erase(
            std::remove_if(hangedManParticles_.begin(), hangedManParticles_.end(),
                [](const HangedManParticle& hp) { return hp.life >= hp.maxLife; }),
            hangedManParticles_.end());
    }

    if (devilAnimPending_) {
        const float dElapsed = devilAnimClock_.getElapsedTime().asSeconds();
        constexpr float kSealEnd = 0.80f;
        constexpr float kConsumeEnd = 1.70f;
        constexpr float kAftermathEnd = 2.40f;
        static thread_local std::mt19937 dRng(std::random_device{}());

        if (dElapsed < kSealEnd) {
            // Phase 1: Contract descends — pentagram appears, dark overlay
            const float t = dElapsed / kSealEnd;
            devilOverlayAlpha_ = t * 0.35f;
            devilPentagramAlpha_ = t;
            devilPentagramScale_ = 0.5f + t * 1.5f;
            devilPentagramAngle_ = t * 3.14159265f * 0.4f;
            devilFlameIntensity_ = 0.0f;
            // Rune particles spiral outward
            if (devilParticles_.size() < 30 && devilTargetPos_.x >= 0) {
                const auto tpx = board_.cellToPixel(devilTargetPos_.x, devilTargetPos_.y);
                for (int i = 0; i < 1; ++i) {
                    DevilParticle dp;
                    const float angle = static_cast<float>(dRng()) / dRng.max() * 2.0f * 3.14159265f;
                    const float radius = 10.0f + t * 40.0f;
                    dp.pos = {static_cast<float>(tpx.x + std::cos(angle) * radius),
                              static_cast<float>(tpx.y + std::sin(angle) * radius)};
                    dp.vel = {static_cast<float>(std::cos(angle) * 15.0f + std::uniform_real_distribution<>(-8.0, 8.0)(dRng)),
                              static_cast<float>(std::sin(angle) * 15.0f + std::uniform_real_distribution<>(-8.0, 8.0)(dRng))};
                    dp.life = 0.0f;
                    dp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.6, 1.5)(dRng));
                    dp.scale = static_cast<float>(std::uniform_real_distribution<>(0.5, 1.5)(dRng));
                    dp.isEmber = false;
                    devilParticles_.push_back(dp);
                }
            }
        } else if (dElapsed < kConsumeEnd) {
            // Phase 2: Contract execution — dark flames consume piece
            const float t = (dElapsed - kSealEnd) / (kConsumeEnd - kSealEnd);
            devilOverlayAlpha_ = 0.35f + t * 0.20f;
            devilPentagramAlpha_ = 1.0f - t * 0.3f;
            devilPentagramScale_ = 2.0f - t * 0.5f;
            devilPentagramAngle_ += (1.0f - t) * 1.5f * (1.0f / 60.0f);
            devilFlameIntensity_ = t;
            // Remove piece at 35% through phase 2
            if (!devilPieceRemoved_ && t > 0.35f && devilTargetPos_.x >= 0) {
                devilPieceRemoved_ = true;
                board_.removePieceAt(devilTargetPos_.x, devilTargetPos_.y);
            }
            // Dark flame particles erupt from target
            if (devilTargetPos_.x >= 0 && devilParticles_.size() < 80) {
                const auto tpx = board_.cellToPixel(devilTargetPos_.x, devilTargetPos_.y);
                for (int i = 0; i < 3; ++i) {
                    DevilParticle dp;
                    dp.pos = {static_cast<float>(tpx.x + std::uniform_real_distribution<>(-12.0, 12.0)(dRng)),
                              static_cast<float>(tpx.y + std::uniform_real_distribution<>(-12.0, 12.0)(dRng))};
                    dp.vel = {static_cast<float>(std::uniform_real_distribution<>(-40.0, 40.0)(dRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-80.0, -20.0)(dRng))};
                    dp.life = 0.0f;
                    dp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.5, 1.2)(dRng));
                    dp.scale = static_cast<float>(std::uniform_real_distribution<>(0.6, 2.0)(dRng));
                    dp.isEmber = false;
                    devilParticles_.push_back(dp);
                }
            }
        } else if (dElapsed < kAftermathEnd) {
            // Phase 3: Aftermath — pentagram fragments, embers rise
            const float t = (dElapsed - kConsumeEnd) / (kAftermathEnd - kConsumeEnd);
            devilOverlayAlpha_ = 0.55f * (1.0f - t);
            devilPentagramAlpha_ = 0.7f * (1.0f - t);
            devilPentagramScale_ = 1.5f + t * 1.0f;
            devilPentagramAngle_ += 0.5f * (1.0f / 60.0f);
            devilFlameIntensity_ = 1.0f - t;
            devilScorchAlpha_ = (1.0f - t) * 0.5f;
            // Ember ash particles
            if (devilTargetPos_.x >= 0 && devilParticles_.size() < 100) {
                const auto tpx = board_.cellToPixel(devilTargetPos_.x, devilTargetPos_.y);
                for (int i = 0; i < 1; ++i) {
                    DevilParticle dp;
                    dp.pos = {static_cast<float>(tpx.x + std::uniform_real_distribution<>(-20.0, 20.0)(dRng)),
                              static_cast<float>(tpx.y + std::uniform_real_distribution<>(-20.0, 20.0)(dRng))};
                    dp.vel = {static_cast<float>(std::uniform_real_distribution<>(-20.0, 20.0)(dRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-30.0, -5.0)(dRng))};
                    dp.life = 0.0f;
                    dp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.8, 2.0)(dRng));
                    dp.scale = static_cast<float>(std::uniform_real_distribution<>(0.3, 1.0)(dRng));
                    dp.isEmber = true;
                    devilParticles_.push_back(dp);
                }
            }
        } else {
            devilAnimPending_ = false;
            devilOverlayAlpha_ = 0.0f;
            devilPentagramAlpha_ = 0.0f;
            devilPentagramScale_ = 1.0f;
            devilPentagramAngle_ = 0.0f;
            devilFlameIntensity_ = 0.0f;
            devilScorchAlpha_ = 0.0f;
            devilParticles_.clear();
            devilTargetPos_ = {-1, -1};
        }

        // Tick particles
        for (auto& dp : devilParticles_) {
            dp.life += 1.0f / 60.0f;
            dp.pos += dp.vel * (1.0f / 60.0f);
        }
        devilParticles_.erase(
            std::remove_if(devilParticles_.begin(), devilParticles_.end(),
                [](const DevilParticle& dp) { return dp.life >= dp.maxLife; }),
            devilParticles_.end());
    }

    if (chariotAnimPending_) {
        const float cElapsed = chariotAnimClock_.getElapsedTime().asSeconds();
        constexpr float kChargeEnd = 0.50f;
        constexpr float kImpactEnd = 1.10f;
        constexpr float kSettleEnd = 1.80f;
        static thread_local std::mt19937 cRng(std::random_device{}());

        if (cElapsed < kChargeEnd) {
            // Phase 1: Charge — golden gear appears, particles gather
            const float t = cElapsed / kChargeEnd;
            chariotOverlayAlpha_ = t * 0.20f;
            chariotChargeAlpha_ = t;
            chariotGearAngle_ = t * 3.14159265f * 3.0f;
            // Dust particles kicked up near source
            if (chariotParticles_.size() < 25 && chariotPushSource_.x >= 0) {
                const auto spx = board_.cellToPixel(chariotPushSource_.x, chariotPushSource_.y);
                for (int i = 0; i < 1; ++i) {
                    ChariotParticle cp;
                    cp.pos = {static_cast<float>(spx.x + std::uniform_real_distribution<>(-14.0, 14.0)(cRng)),
                              static_cast<float>(spx.y + std::uniform_real_distribution<>(-14.0, 14.0)(cRng))};
                    cp.vel = {static_cast<float>(std::uniform_real_distribution<>(-15.0, 15.0)(cRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-25.0, -5.0)(cRng))};
                    cp.life = 0.0f;
                    cp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.4, 1.0)(cRng));
                    cp.scale = static_cast<float>(std::uniform_real_distribution<>(0.5, 1.5)(cRng));
                    chariotParticles_.push_back(cp);
                }
            }
        } else if (cElapsed < kImpactEnd) {
            // Phase 2: Impact — push piece, place player piece
            const float t = (cElapsed - kChargeEnd) / (kImpactEnd - kChargeEnd);
            chariotOverlayAlpha_ = 0.20f + t * 0.15f;
            chariotChargeAlpha_ = 1.0f - t;
            chariotGearAngle_ += 6.0f * (1.0f / 60.0f);
            chariotImpactAlpha_ = t < 0.4f ? t / 0.4f : 1.0f - (t - 0.4f) / 0.6f;
            chariotTrailAlpha_ = t;
            // Remove opponent from source & push to dest at 20% through phase
            if (!chariotPiecePushed_ && t > 0.20f && chariotPushSource_.x >= 0) {
                chariotPiecePushed_ = true;
                board_.removePieceAt(chariotPushSource_.x, chariotPushSource_.y);
                board_.placePieceAt(chariotPushDest_.x, chariotPushDest_.y, nextTurn(currentTurn_));
            }
            // Place player piece at source at 50% through phase
            if (!chariotPiecePlaced_ && t > 0.50f && chariotPlayerPos_.x >= 0) {
                chariotPiecePlaced_ = true;
                board_.placePieceAt(chariotPlayerPos_.x, chariotPlayerPos_.y, currentTurn_);
                spawnSparks(board_.cellToPixel(chariotPlayerPos_.x, chariotPlayerPos_.y), currentTurn_);
                starHighlightValid_ = false;
                winner_ = board_.winnerFromLastMove();
                if (winner_ != Board::Piece::None) gameOver_ = true;
            }
            // Impact particles from source
            if (chariotPiecePushed_ && chariotParticles_.size() < 60) {
                const auto spx = board_.cellToPixel(chariotPushSource_.x, chariotPushSource_.y);
                const auto dpx = board_.cellToPixel(chariotPushDest_.x, chariotPushDest_.y);
                const sf::Vector2f pushDir = {
                    static_cast<float>(dpx.x - spx.x),
                    static_cast<float>(dpx.y - spx.y)};
                const float pLen = std::sqrt(pushDir.x * pushDir.x + pushDir.y * pushDir.y);
                const sf::Vector2f pn = pLen > 0.001f ? sf::Vector2f(pushDir.x / pLen, pushDir.y / pLen) : sf::Vector2f(0.0f, -1.0f);
                for (int i = 0; i < 2; ++i) {
                    ChariotParticle cp;
                    cp.pos = {static_cast<float>(spx.x + std::uniform_real_distribution<>(-10.0, 10.0)(cRng)),
                              static_cast<float>(spx.y + std::uniform_real_distribution<>(-10.0, 10.0)(cRng))};
                    cp.vel = {static_cast<float>(pn.x * std::uniform_real_distribution<>(30.0, 80.0)(cRng) + std::uniform_real_distribution<>(-15.0, 15.0)(cRng)),
                              static_cast<float>(pn.y * std::uniform_real_distribution<>(30.0, 80.0)(cRng) + std::uniform_real_distribution<>(-15.0, 15.0)(cRng))};
                    cp.life = 0.0f;
                    cp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.3, 0.8)(cRng));
                    cp.scale = static_cast<float>(std::uniform_real_distribution<>(0.5, 1.5)(cRng));
                    chariotParticles_.push_back(cp);
                }
            }
        } else if (cElapsed < kSettleEnd) {
            // Phase 3: Settle — particles fade, trail remains
            const float t = (cElapsed - kImpactEnd) / (kSettleEnd - kImpactEnd);
            chariotOverlayAlpha_ = 0.35f * (1.0f - t);
            chariotChargeAlpha_ = 0.0f;
            chariotImpactAlpha_ = 0.0f;
            chariotTrailAlpha_ = 1.0f - t;
        } else {
            // Post-placement logic (deferred from tryPlacePiece)
            if (!gameOver_) {
                // Check card event trigger
                const int totalPieces = board_.totalPieceCount();
                if (totalPieces >= nextCardThreshold_) {
                    triggerCardEvent();
                    nextCardThreshold_ += 8;
                    cardDeferredTurn_ = true;
                } else {
                    currentTurn_ = nextTurn(currentTurn_);
                }
                // Decrement turn-based effects
                if (hierophantRemaining_ > 0) --hierophantRemaining_;
                if (hermitRemaining_ > 0) --hermitRemaining_;
                if (temperanceRemaining_ > 0) {
                    --temperanceRemaining_;
                    if (temperanceRemaining_ == 0) board_.setTemperanceActive(false);
                }
                updateHardModeObstacles();
            }
            chariotAnimPending_ = false;
            chariotOverlayAlpha_ = 0.0f;
            chariotChargeAlpha_ = 0.0f;
            chariotGearAngle_ = 0.0f;
            chariotImpactAlpha_ = 0.0f;
            chariotTrailAlpha_ = 0.0f;
            chariotParticles_.clear();
            chariotPushSource_ = {-1, -1};
            chariotPushDest_ = {-1, -1};
            chariotPlayerPos_ = {-1, -1};
        }

        // Tick particles
        for (auto& cp : chariotParticles_) {
            cp.life += 1.0f / 60.0f;
            cp.pos += cp.vel * (1.0f / 60.0f);
        }
        chariotParticles_.erase(
            std::remove_if(chariotParticles_.begin(), chariotParticles_.end(),
                [](const ChariotParticle& cp) { return cp.life >= cp.maxLife; }),
            chariotParticles_.end());
    }

    if (magicianAnimPending_) {
        const float mElapsed = magicianAnimClock_.getElapsedTime().asSeconds();
        constexpr float kRippleEnd = 0.50f;
        constexpr float kArcEnd = 1.20f;
        constexpr float kSpawnEnd = 1.80f;
        static thread_local std::mt19937 mRng(std::random_device{}());

        const auto cellPx = [&](sf::Vector2i gp) { return board_.cellToPixel(gp.x, gp.y); };
        const auto spx = cellPx(magicianSourcePos_);
        const auto tpx = cellPx(magicianTargetPos_);

        if (mElapsed < kRippleEnd) {
            // Phase 1: Ripple at source
            const float t = mElapsed / kRippleEnd;
            magicianOverlayAlpha_ = t * 0.25f;
            magicianRippleAlpha_ = t;
            magicianArcProgress_ = 0.0f;
            // Diamond particles from source
            if (magicianParticles_.size() < 20 && magicianSourcePos_.x >= 0) {
                for (int i = 0; i < 1; ++i) {
                    MagicianParticle mp;
                    mp.pos = {static_cast<float>(spx.x + std::uniform_real_distribution<>(-10.0, 10.0)(mRng)),
                              static_cast<float>(spx.y + std::uniform_real_distribution<>(-10.0, 10.0)(mRng))};
                    mp.vel = {static_cast<float>(std::uniform_real_distribution<>(-20.0, 20.0)(mRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-40.0, -15.0)(mRng))};
                    mp.life = 0.0f;
                    mp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.5, 1.2)(mRng));
                    mp.scale = static_cast<float>(std::uniform_real_distribution<>(0.5, 1.5)(mRng));
                    mp.type = 0; // diamond
                    magicianParticles_.push_back(mp);
                }
            }
        } else if (mElapsed < kArcEnd) {
            // Phase 2: Magic arc from source to target
            const float t = (mElapsed - kRippleEnd) / (kArcEnd - kRippleEnd);
            magicianOverlayAlpha_ = 0.25f + t * 0.10f;
            magicianRippleAlpha_ = 1.0f - t;
            magicianArcProgress_ = t;
            magicianPortalAlpha_ = t < 0.6f ? 0.0f : (t - 0.6f) / 0.4f;
            // Arc trail particles
            if (magicianParticles_.size() < 50 && magicianSourcePos_.x >= 0 && magicianTargetPos_.x >= 0) {
                const float arcT = t;
                // Point along bezier curve
                const sf::Vector2f mid = {
                    (spx.x + tpx.x) * 0.5f,
                    (spx.y + tpx.y) * 0.5f - 80.0f
                };
                const auto bezier = [](float t, sf::Vector2f a, sf::Vector2f b, sf::Vector2f c) {
                    return a * (1.0f - t) * (1.0f - t) + b * 2.0f * (1.0f - t) * t + c * t * t;
                };
                const sf::Vector2f bp = bezier(arcT, spx, mid, tpx);
                for (int i = 0; i < 1; ++i) {
                    MagicianParticle mp;
                    mp.pos = {static_cast<float>(bp.x + std::uniform_real_distribution<>(-6.0, 6.0)(mRng)),
                              static_cast<float>(bp.y + std::uniform_real_distribution<>(-6.0, 6.0)(mRng))};
                    mp.vel = {static_cast<float>(std::uniform_real_distribution<>(-10.0, 10.0)(mRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-15.0, 5.0)(mRng))};
                    mp.life = 0.0f;
                    mp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.3, 0.7)(mRng));
                    mp.scale = static_cast<float>(std::uniform_real_distribution<>(0.4, 1.0)(mRng));
                    mp.type = 1; // sparkle
                    magicianParticles_.push_back(mp);
                }
            }
        } else if (mElapsed < kSpawnEnd) {
            // Phase 3: Materialize piece at target
            const float t = (mElapsed - kArcEnd) / (kSpawnEnd - kArcEnd);
            magicianOverlayAlpha_ = 0.35f * (1.0f - t);
            magicianRippleAlpha_ = 0.0f;
            magicianArcProgress_ = 1.0f;
            magicianPortalAlpha_ = 1.0f - t;
            magicianSpawnAlpha_ = t;
            // Place piece at 40% through phase 3
            if (!magicianPiecePlaced_ && t > 0.40f && magicianTargetPos_.x >= 0) {
                magicianPiecePlaced_ = true;
                const Board::Piece playerPiece = Board::Piece::Black;
                board_.placePieceAt(magicianTargetPos_.x, magicianTargetPos_.y, playerPiece);
                spawnSparks(tpx, playerPiece);
                winner_ = board_.winnerFromLastMove();
                if (winner_ != Board::Piece::None) gameOver_ = true;
            }
            // Burst particles from target
            if (magicianPiecePlaced_ && magicianParticles_.size() < 80 && magicianTargetPos_.x >= 0) {
                for (int i = 0; i < 2; ++i) {
                    MagicianParticle mp;
                    const float angle = static_cast<float>(mRng()) / mRng.max() * 2.0f * 3.14159265f;
                    const float speed = static_cast<float>(std::uniform_real_distribution<>(20.0, 60.0)(mRng));
                    mp.pos = {static_cast<float>(tpx.x + std::uniform_real_distribution<>(-8.0, 8.0)(mRng)),
                              static_cast<float>(tpx.y + std::uniform_real_distribution<>(-8.0, 8.0)(mRng))};
                    mp.vel = {static_cast<float>(std::cos(angle) * speed),
                              static_cast<float>(std::sin(angle) * speed)};
                    mp.life = 0.0f;
                    mp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.4, 1.0)(mRng));
                    mp.scale = static_cast<float>(std::uniform_real_distribution<>(0.5, 1.8)(mRng));
                    mp.type = static_cast<int>(mRng() % 2); // mix of diamond and sparkle
                    magicianParticles_.push_back(mp);
                }
            }
        } else {
            magicianAnimPending_ = false;
            magicianOverlayAlpha_ = 0.0f;
            magicianRippleAlpha_ = 0.0f;
            magicianArcProgress_ = 0.0f;
            magicianPortalAlpha_ = 0.0f;
            magicianSpawnAlpha_ = 0.0f;
            magicianParticles_.clear();
            magicianSourcePos_ = {-1, -1};
            magicianTargetPos_ = {-1, -1};
        }

        // Tick particles
        for (auto& mp : magicianParticles_) {
            mp.life += 1.0f / 60.0f;
            mp.pos += mp.vel * (1.0f / 60.0f);
        }
        magicianParticles_.erase(
            std::remove_if(magicianParticles_.begin(), magicianParticles_.end(),
                [](const MagicianParticle& mp) { return mp.life >= mp.maxLife; }),
            magicianParticles_.end());
    }

    if (temperanceAnimPending_) {
        const float tElapsed = temperanceAnimClock_.getElapsedTime().asSeconds();
        constexpr float kWingsEnd = 0.70f;
        constexpr float kHaloEnd = 1.50f;
        constexpr float kSettleEnd = 2.20f;
        static thread_local std::mt19937 tRng(std::random_device{}());
        const auto boardCenter = board_.cellToPixel(7, 7);

        if (tElapsed < kWingsEnd) {
            // Phase 1: Angel wings descend
            const float t = tElapsed / kWingsEnd;
            temperanceOverlayAlpha_ = t * 0.22f;
            temperanceWingAlpha_ = t;
            temperanceHaloRadius_ = 0.0f;
            temperanceHaloAlpha_ = 0.0f;
            temperanceMarkAlpha_ = 0.0f;
            // Soft falling particles from upper area
            if (temperanceParticles_.size() < 30) {
                for (int i = 0; i < 1; ++i) {
                    TemperanceParticle tp;
                    tp.pos = {static_cast<float>(std::uniform_real_distribution<>(100.0, 1180.0)(tRng)),
                              static_cast<float>(std::uniform_real_distribution<>(0.0, 120.0)(tRng))};
                    tp.vel = {static_cast<float>(std::uniform_real_distribution<>(-15.0, 15.0)(tRng)),
                              static_cast<float>(std::uniform_real_distribution<>(15.0, 40.0)(tRng))};
                    tp.life = 0.0f;
                    tp.maxLife = static_cast<float>(std::uniform_real_distribution<>(1.0, 2.5)(tRng));
                    tp.scale = static_cast<float>(std::uniform_real_distribution<>(0.5, 1.8)(tRng));
                    temperanceParticles_.push_back(tp);
                }
            }
        } else if (tElapsed < kHaloEnd) {
            // Phase 2: Halo expands, sweeps obstacles
            const float t = (tElapsed - kWingsEnd) / (kHaloEnd - kWingsEnd);
            temperanceOverlayAlpha_ = 0.22f + t * 0.08f;
            temperanceWingAlpha_ = 1.0f - t * 0.6f;
            temperanceHaloRadius_ = 80.0f + t * 500.0f; // expanding ring
            temperanceHaloAlpha_ = 1.0f - t * 0.5f;
            temperanceMarkAlpha_ = t;
        } else if (tElapsed < kSettleEnd) {
            // Phase 3: Settle, wings fade, mark remains
            const float t = (tElapsed - kHaloEnd) / (kSettleEnd - kHaloEnd);
            temperanceOverlayAlpha_ = 0.30f * (1.0f - t);
            temperanceWingAlpha_ = 0.4f * (1.0f - t);
            temperanceHaloAlpha_ = 0.5f * (1.0f - t);
            temperanceHaloRadius_ = 580.0f;
            temperanceMarkAlpha_ = 1.0f;
            // Remnant particles near board center
            if (temperanceParticles_.size() < 50) {
                for (int i = 0; i < 1; ++i) {
                    TemperanceParticle tp;
                    tp.pos = {static_cast<float>(boardCenter.x + std::uniform_real_distribution<>(-60.0, 60.0)(tRng)),
                              static_cast<float>(boardCenter.y + std::uniform_real_distribution<>(-60.0, 60.0)(tRng))};
                    tp.vel = {static_cast<float>(std::uniform_real_distribution<>(-8.0, 8.0)(tRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-10.0, -2.0)(tRng))};
                    tp.life = 0.0f;
                    tp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.5, 1.5)(tRng));
                    tp.scale = static_cast<float>(std::uniform_real_distribution<>(0.3, 1.0)(tRng));
                    temperanceParticles_.push_back(tp);
                }
            }
        } else {
            temperanceAnimPending_ = false;
            temperanceOverlayAlpha_ = 0.0f;
            temperanceWingAlpha_ = 0.0f;
            temperanceHaloAlpha_ = 0.0f;
            temperanceHaloRadius_ = 0.0f;
            // Mark persists while effect active; fade if expired
            if (temperanceRemaining_ <= 0) {
                temperanceMarkAlpha_ = 0.0f;
            }
            temperanceParticles_.clear();
        }

        // Tick particles
        for (auto& tp : temperanceParticles_) {
            tp.life += 1.0f / 60.0f;
            tp.pos += tp.vel * (1.0f / 60.0f);
        }
        temperanceParticles_.erase(
            std::remove_if(temperanceParticles_.begin(), temperanceParticles_.end(),
                [](const TemperanceParticle& tp) { return tp.life >= tp.maxLife; }),
            temperanceParticles_.end());
    }

    // Update persistent temperance mark rotation (while effect active)
    if (temperanceRemaining_ > 0 && temperanceMarkAlpha_ > 0.001f) {
        temperanceMarkAngle_ += 0.4f * (1.0f / 60.0f);
    }
    // Fade mark when effect expires
    if (temperanceRemaining_ <= 0 && temperanceMarkAlpha_ > 0.001f) {
        temperanceMarkAlpha_ -= 1.0f / 60.0f;
        if (temperanceMarkAlpha_ <= 0.001f) temperanceMarkAlpha_ = 0.0f;
    }

    if (foolAnimPending_) {
        const float fElapsed = foolAnimClock_.getElapsedTime().asSeconds();
        constexpr float kLandEnd = 0.55f;
        constexpr float kBurstEnd = 1.00f;
        constexpr float kSettleEnd = 1.60f;
        static thread_local std::mt19937 fRng(std::random_device{}());
        const sf::Vector2f boardCenter = {640.0f, 450.0f};

        if (fElapsed < kLandEnd) {
            // Phase 1: Star bounces down from top to board center
            const float t = fElapsed / kLandEnd;
            foolOverlayAlpha_ = t * 0.15f;
            foolStarAlpha_ = t;
            foolStarScale_ = 0.5f + t * 1.5f;
            foolStarAngle_ = t * 3.14159265f * 4.0f;
            foolStarBounceT_ = t;
            // Bounce curve: damped sine
            const float bounce = std::abs(std::sin(t * 3.14159265f * 3.0f)) * std::exp(-t * 3.0f);
            foolStarPos_.x = 640.0f + (1.0f - t) * std::sin(t * 8.0f) * 80.0f;
            foolStarPos_.y = 120.0f + (boardCenter.y - 120.0f) * t - bounce * 120.0f;
            // Trail particles behind star
            if (foolParticles_.size() < 30 && t > 0.1f) {
                for (int i = 0; i < 1; ++i) {
                    FoolParticle fp;
                    fp.pos = foolStarPos_;
                    fp.vel = {static_cast<float>(std::uniform_real_distribution<>(-10.0, 10.0)(fRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-5.0, 20.0)(fRng))};
                    fp.life = 0.0f;
                    fp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.3, 0.8)(fRng));
                    fp.scale = static_cast<float>(std::uniform_real_distribution<>(0.4, 1.2)(fRng));
                    fp.hue = static_cast<float>(t + std::uniform_real_distribution<>(-0.1, 0.1)(fRng));
                    foolParticles_.push_back(fp);
                }
            }
        } else if (fElapsed < kBurstEnd) {
            // Phase 2: Burst — star at center, explosion of rainbow particles
            const float t = (fElapsed - kLandEnd) / (kBurstEnd - kLandEnd);
            foolOverlayAlpha_ = 0.15f + t * 0.05f;
            foolStarAlpha_ = 1.0f;
            foolStarScale_ = 2.0f + t * 0.5f;
            foolStarAngle_ += 5.0f * (1.0f / 60.0f);
            foolStarBounceT_ = 1.0f;
            foolStarPos_ = boardCenter;
            foolBurstAlpha_ = t < 0.4f ? t / 0.4f : 1.0f - (t - 0.4f) / 0.6f;
            // Rainbow burst particles
            if (foolParticles_.size() < 80) {
                for (int i = 0; i < 3; ++i) {
                    FoolParticle fp;
                    const float angle = static_cast<float>(fRng()) / fRng.max() * 2.0f * 3.14159265f;
                    const float speed = static_cast<float>(std::uniform_real_distribution<>(30.0, 100.0)(fRng));
                    fp.pos = {static_cast<float>(boardCenter.x + std::uniform_real_distribution<>(-10.0, 10.0)(fRng)),
                              static_cast<float>(boardCenter.y + std::uniform_real_distribution<>(-10.0, 10.0)(fRng))};
                    fp.vel = {static_cast<float>(std::cos(angle) * speed),
                              static_cast<float>(std::sin(angle) * speed)};
                    fp.life = 0.0f;
                    fp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.4, 1.0)(fRng));
                    fp.scale = static_cast<float>(std::uniform_real_distribution<>(0.5, 2.0)(fRng));
                    fp.hue = static_cast<float>(fRng()) / fRng.max();
                    foolParticles_.push_back(fp);
                }
            }
        } else if (fElapsed < kSettleEnd) {
            // Phase 3: Star shrinks and settles
            const float t = (fElapsed - kBurstEnd) / (kSettleEnd - kBurstEnd);
            foolOverlayAlpha_ = 0.20f * (1.0f - t);
            foolStarScale_ = 2.5f * (1.0f - t) + 0.6f * t;
            foolStarAlpha_ = 1.0f;
            foolStarAngle_ += 2.0f * (1.0f / 60.0f);
            foolBurstAlpha_ = 0.0f;
            // Move star toward indicator position (top-right of board area)
            const sf::Vector2f indicatorPos = {1100.0f, 60.0f};
            foolStarPos_.x = boardCenter.x + (indicatorPos.x - boardCenter.x) * t;
            foolStarPos_.y = boardCenter.y + (indicatorPos.y - boardCenter.y) * t;
        } else {
            foolAnimPending_ = false;
            foolOverlayAlpha_ = 0.0f;
            foolBurstAlpha_ = 0.0f;
            foolParticles_.clear();
            // Star persists at indicator position while foolActive_
            foolStarPos_ = {1100.0f, 60.0f};
            foolStarScale_ = 0.6f;
            foolStarAlpha_ = 1.0f;
            foolStarAngle_ = 0.0f;
        }

        // Tick particles
        for (auto& fp : foolParticles_) {
            fp.life += 1.0f / 60.0f;
            fp.pos += fp.vel * (1.0f / 60.0f);
        }
        foolParticles_.erase(
            std::remove_if(foolParticles_.begin(), foolParticles_.end(),
                [](const FoolParticle& fp) { return fp.life >= fp.maxLife; }),
            foolParticles_.end());
    }

    // Persistent star rotation while fool is active (even after animation)
    if (foolActive_ && !foolAnimPending_ && foolStarAlpha_ > 0.001f) {
        foolStarAngle_ += 0.6f * (1.0f / 60.0f);
    }
    // Fade star when fool is consumed
    if (!foolActive_ && !foolAnimPending_ && foolStarAlpha_ > 0.001f) {
        foolStarAlpha_ -= 1.0f / 30.0f;
        if (foolStarAlpha_ <= 0.001f) foolStarAlpha_ = 0.0f;
    }

    if (towerAnimPending_) {
        const float twElapsed = towerAnimClock_.getElapsedTime().asSeconds();
        constexpr float kLightningEnd = 0.60f;
        constexpr float kCollapseEnd = 1.50f;
        constexpr float kRebirthEnd = 2.50f;
        static thread_local std::mt19937 twRng(std::random_device{}());

        if (twElapsed < kLightningEnd) {
            // Phase 1: Lightning strike + flash
            const float t = twElapsed / kLightningEnd;
            towerFlashAlpha_ = t < 0.15f ? t / 0.15f * 0.7f : 0.7f * (1.0f - (t - 0.15f) / 0.85f);
            towerLightningAlpha_ = t;
            towerRubbleAlpha_ = 0.0f;
            towerRebirthAlpha_ = 0.0f;
            shakeIntensity_ = towerFlashAlpha_ * 8.0f;
        } else if (twElapsed < kCollapseEnd) {
            // Phase 2: Obstacles crumble into rubble
            const float t = (twElapsed - kLightningEnd) / (kCollapseEnd - kLightningEnd);
            towerFlashAlpha_ = 0.7f * (1.0f - t);
            towerLightningAlpha_ = 1.0f - t;
            towerRubbleAlpha_ = t;
            towerRebirthAlpha_ = 0.0f;
            shakeIntensity_ = (1.0f - t) * 6.0f + 1.0f;
            // Clear obstacles at 55% through phase
            if (!towerObstaclesCleared_ && t > 0.55f) {
                towerObstaclesCleared_ = true;
                board_.clearObstacles();
            }
            // Rubble particles from saved obstacle positions
            if (towerObstaclesCleared_ && towerParticles_.size() < 100) {
                for (const auto& so : towerSavedObstacles_) {
                    if (towerParticles_.size() >= 100) break;
                    const auto opx = board_.cellToPixel(static_cast<int>(so.x), static_cast<int>(so.y));
                    for (int i = 0; i < 2; ++i) {
                        TowerParticle tp;
                        tp.pos = {static_cast<float>(opx.x + std::uniform_real_distribution<>(-8.0, 8.0)(twRng)),
                                  static_cast<float>(opx.y + std::uniform_real_distribution<>(-8.0, 8.0)(twRng))};
                        const float angle = static_cast<float>(twRng()) / twRng.max() * 2.0f * 3.14159265f;
                        const float speed = static_cast<float>(std::uniform_real_distribution<>(40.0, 120.0)(twRng));
                        tp.vel = {static_cast<float>(std::cos(angle) * speed),
                                  static_cast<float>(std::sin(angle) * speed * 0.6f - 30.0f)};
                        tp.life = 0.0f;
                        tp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.4, 1.2)(twRng));
                        tp.scale = static_cast<float>(std::uniform_real_distribution<>(0.5, 2.0)(twRng));
                        tp.isRubble = true;
                        towerParticles_.push_back(tp);
                    }
                }
            }
        } else if (twElapsed < kRebirthEnd) {
            // Phase 3: New obstacles rise from the ground
            const float t = (twElapsed - kCollapseEnd) / (kRebirthEnd - kCollapseEnd);
            towerFlashAlpha_ = 0.0f;
            towerLightningAlpha_ = 0.0f;
            towerRubbleAlpha_ = 1.0f - t;
            towerRebirthAlpha_ = t;
            shakeIntensity_ = (1.0f - t) * 1.0f;
            // Generate new obstacles at 20% through phase
            if (!towerObstaclesRegenerated_ && t > 0.20f) {
                towerObstaclesRegenerated_ = true;
                board_.generateRandomObstacles(towerObstacleCount_);
            }
            // Golden energy motes from new obstacle positions
            if (towerObstaclesRegenerated_ && towerParticles_.size() < 150) {
                for (const auto& op : board_.obstaclePositions()) {
                    if (towerParticles_.size() >= 150) break;
                    const auto opx = board_.cellToPixel(op.x, op.y);
                    for (int i = 0; i < 1; ++i) {
                        TowerParticle tp;
                        tp.pos = {static_cast<float>(opx.x + std::uniform_real_distribution<>(-6.0, 6.0)(twRng)),
                                  static_cast<float>(opx.y + std::uniform_real_distribution<>(-6.0, 6.0)(twRng))};
                        tp.vel = {static_cast<float>(std::uniform_real_distribution<>(-15.0, 15.0)(twRng)),
                                  static_cast<float>(std::uniform_real_distribution<>(-40.0, -10.0)(twRng))};
                        tp.life = 0.0f;
                        tp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.4, 0.8)(twRng));
                        tp.scale = static_cast<float>(std::uniform_real_distribution<>(0.3, 1.0)(twRng));
                        tp.isRubble = false;
                        towerParticles_.push_back(tp);
                    }
                }
            }
        } else {
            towerAnimPending_ = false;
            towerFlashAlpha_ = 0.0f;
            towerLightningAlpha_ = 0.0f;
            towerRubbleAlpha_ = 0.0f;
            towerRebirthAlpha_ = 0.0f;
            towerParticles_.clear();
            towerSavedObstacles_.clear();
            shakeIntensity_ = 0.0f;
        }

        // Tick particles
        for (auto& tp : towerParticles_) {
            tp.life += 1.0f / 60.0f;
            tp.pos += tp.vel * (1.0f / 60.0f);
            if (tp.isRubble) {
                tp.vel.y += 120.0f * (1.0f / 60.0f); // gravity for rubble
            }
        }
        towerParticles_.erase(
            std::remove_if(towerParticles_.begin(), towerParticles_.end(),
                [](const TowerParticle& tp) { return tp.life >= tp.maxLife; }),
            towerParticles_.end());
    }

    if (wheelFortuneAnimPending_) {
        const float wfElapsed = wheelFortuneAnimClock_.getElapsedTime().asSeconds();
        constexpr float kWheelAppearEnd = 0.60f;
        constexpr float kSpinEnd = 1.50f;
        constexpr float kScatterEnd = 2.20f;
        static thread_local std::mt19937 wfRng(std::random_device{}());
        const auto boardCenter = board_.cellToPixel(7, 7);
        const float wfCenterX = static_cast<float>(boardCenter.x);
        const float wfCenterY = static_cast<float>(boardCenter.y);

        if (wfElapsed < kWheelAppearEnd) {
            // Phase 1: Wheel appears, obstacles glow
            const float t = wfElapsed / kWheelAppearEnd;
            wheelFortuneAlpha_ = t;
            wheelFortuneRadius_ = 40.0f + t * 120.0f;
            wheelFortuneAngle_ = t * 3.14159265f * 2.0f;
            wheelFortuneOrbProgress_ = 0.0f;
            // Gold sparkle particles around wheel
            if (wheelFortuneParticles_.size() < 25) {
                const float angle = static_cast<float>(wfRng()) / wfRng.max() * 2.0f * 3.14159265f;
                const float dist = wheelFortuneRadius_ * static_cast<float>(wfRng()) / wfRng.max();
                WheelFortuneParticle wp;
                wp.pos = {static_cast<float>(wfCenterX + std::cos(angle) * dist),
                          static_cast<float>(wfCenterY + std::sin(angle) * dist)};
                wp.vel = {static_cast<float>(std::uniform_real_distribution<>(-10.0, 10.0)(wfRng)),
                          static_cast<float>(std::uniform_real_distribution<>(-10.0, 10.0)(wfRng))};
                wp.life = 0.0f;
                wp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.4, 0.8)(wfRng));
                wp.scale = static_cast<float>(std::uniform_real_distribution<>(0.5, 1.5)(wfRng));
                wheelFortuneParticles_.push_back(wp);
            }
        } else if (wfElapsed < kSpinEnd) {
            // Phase 2: Wheel spins fast, orbs fly to center
            const float t = (wfElapsed - kWheelAppearEnd) / (kSpinEnd - kWheelAppearEnd);
            wheelFortuneAlpha_ = 1.0f;
            wheelFortuneRadius_ = 160.0f + std::sin(t * 3.14159265f) * 20.0f;
            wheelFortuneAngle_ += 8.0f * (1.0f / 60.0f);
            wheelFortuneOrbProgress_ = t;
            // Clear obstacles at 55% through phase
            if (!wheelFortuneObstaclesCleared_ && t > 0.55f) {
                wheelFortuneObstaclesCleared_ = true;
                board_.clearObstacles();
            }
            // Trail particles from orbs to center
            if (wheelFortuneParticles_.size() < 60) {
                for (const auto& so : wheelFortuneSavedObstacles_) {
                    if (wheelFortuneParticles_.size() >= 60) break;
                    const auto opx = board_.cellToPixel(static_cast<int>(so.x), static_cast<int>(so.y));
                    const sf::Vector2f dir = {wfCenterX - static_cast<float>(opx.x),
                                              wfCenterY - static_cast<float>(opx.y)};
                    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                    if (len < 1.0f) continue;
                    const sf::Vector2f nd = {dir.x / len, dir.y / len};
                    WheelFortuneParticle wp;
                    wp.pos = {static_cast<float>(opx.x + nd.x * len * t),
                              static_cast<float>(opx.y + nd.y * len * t)};
                    wp.vel = {static_cast<float>(std::uniform_real_distribution<>(-5.0, 5.0)(wfRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-5.0, 5.0)(wfRng))};
                    wp.life = 0.0f;
                    wp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.2, 0.5)(wfRng));
                    wp.scale = static_cast<float>(std::uniform_real_distribution<>(0.4, 1.0)(wfRng));
                    wheelFortuneParticles_.push_back(wp);
                }
            }
        } else if (wfElapsed < kScatterEnd) {
            // Phase 3: Wheel slows, orbs scatter to new positions
            const float t = (wfElapsed - kSpinEnd) / (kScatterEnd - kSpinEnd);
            wheelFortuneAlpha_ = 1.0f - t;
            wheelFortuneRadius_ = 180.0f * (1.0f - t);
            wheelFortuneAngle_ += 3.0f * (1.0f - t) * (1.0f / 60.0f);
            wheelFortuneOrbProgress_ = 1.0f;
            // Regenerate obstacles at 20% through phase
            if (!wheelFortuneObstaclesRegenerated_ && t > 0.20f) {
                wheelFortuneObstaclesRegenerated_ = true;
                board_.generateRandomObstacles(wheelFortuneObstacleCount_);
            }
            // Golden motes from new obstacles
            if (wheelFortuneObstaclesRegenerated_ && wheelFortuneParticles_.size() < 100) {
                for (const auto& op : board_.obstaclePositions()) {
                    if (wheelFortuneParticles_.size() >= 100) break;
                    const auto opx = board_.cellToPixel(op.x, op.y);
                    WheelFortuneParticle wp;
                    wp.pos = {static_cast<float>(opx.x + std::uniform_real_distribution<>(-8.0, 8.0)(wfRng)),
                              static_cast<float>(opx.y + std::uniform_real_distribution<>(-8.0, 8.0)(wfRng))};
                    wp.vel = {static_cast<float>(std::uniform_real_distribution<>(-15.0, 15.0)(wfRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-35.0, -8.0)(wfRng))};
                    wp.life = 0.0f;
                    wp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.3, 0.7)(wfRng));
                    wp.scale = static_cast<float>(std::uniform_real_distribution<>(0.3, 1.0)(wfRng));
                    wheelFortuneParticles_.push_back(wp);
                }
            }
        } else {
            wheelFortuneAnimPending_ = false;
            wheelFortuneAlpha_ = 0.0f;
            wheelFortuneRadius_ = 0.0f;
            wheelFortuneAngle_ = 0.0f;
            wheelFortuneOrbProgress_ = 0.0f;
            wheelFortuneParticles_.clear();
            wheelFortuneSavedObstacles_.clear();
        }

        // Tick particles
        for (auto& wp : wheelFortuneParticles_) {
            wp.life += 1.0f / 60.0f;
            wp.pos += wp.vel * (1.0f / 60.0f);
        }
        wheelFortuneParticles_.erase(
            std::remove_if(wheelFortuneParticles_.begin(), wheelFortuneParticles_.end(),
                [](const WheelFortuneParticle& wp) { return wp.life >= wp.maxLife; }),
            wheelFortuneParticles_.end());
    }

    // Judgement card: divine light purification
    if (judgementAnimPending_) {
        static thread_local std::mt19937 jdgRng(std::random_device{}());
        const float jElapsed = judgementAnimClock_.getElapsedTime().asSeconds();
        constexpr float kLightEnd = 0.80f;
        constexpr float kCleanseEnd = 1.60f;
        constexpr float kFadeEnd = 2.20f;

        if (jElapsed < kLightEnd) {
            // Phase 1: Light descends, darkness gathers
            const float t = jElapsed / kLightEnd;
            judgementLightBeamProgress_ = t;
            judgementDarkenAlpha_ = t * 160.0f;
            shakeIntensity_ = t * 8.0f;

            // Emit particles rising from obstacle positions
            for (const auto& so : judgementSavedObstacles_) {
                const auto opx = board_.cellToPixel(static_cast<int>(so.x), static_cast<int>(so.y));
                const int numP = std::uniform_int_distribution<>(1, 3)(jdgRng);
                for (int i = 0; i < numP; ++i) {
                    JudgementParticle jp;
                    jp.pos = {static_cast<float>(opx.x + std::uniform_real_distribution<>(-8.0, 8.0)(jdgRng)),
                              static_cast<float>(opx.y + std::uniform_real_distribution<>(-6.0, 6.0)(jdgRng))};
                    jp.vel = {static_cast<float>(std::uniform_real_distribution<>(-20.0, 20.0)(jdgRng)),
                              static_cast<float>(std::uniform_real_distribution<>(-80.0, -25.0)(jdgRng))};
                    jp.life = 0.0f;
                    jp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.5, 1.2)(jdgRng));
                    jp.size = static_cast<float>(std::uniform_real_distribution<>(1.5, 3.5)(jdgRng));
                    judgementParticles_.push_back(jp);
                }
            }

            // Emit expanding ring pulses from center every ~0.15s
            if (std::fmod(jElapsed, 0.15f) < 0.016f) {
                JudgementRing jr;
                jr.radius = 15.0f;
                jr.maxRadius = static_cast<float>(std::uniform_real_distribution<>(280.0, 400.0)(jdgRng));
                jr.alpha = 1.0f;
                judgementRings_.push_back(jr);
            }
        } else if (jElapsed < kCleanseEnd) {
            // Phase 2: The Cleansing
            const float t = (jElapsed - kLightEnd) / (kCleanseEnd - kLightEnd);
            judgementLightBeamProgress_ = 1.0f;
            judgementDarkenAlpha_ = 160.0f - t * 80.0f;
            if (t < 0.5f) {
                shakeIntensity_ = 8.0f + t * 8.0f;
            } else {
                shakeIntensity_ = 12.0f * (1.0f - (t - 0.5f) * 2.0f);
            }

            // Clear obstacles once at start of phase 2
            if (!judgementObstaclesCleared_) {
                board_.clearObstacles();
                judgementObstaclesCleared_ = true;
                // Burst particles from each cleared position
                for (const auto& so : judgementSavedObstacles_) {
                    const auto opx = board_.cellToPixel(static_cast<int>(so.x), static_cast<int>(so.y));
                    const int numP = std::uniform_int_distribution<>(8, 18)(jdgRng);
                    for (int i = 0; i < numP; ++i) {
                        JudgementParticle jp;
                        jp.pos = {static_cast<float>(opx.x + std::uniform_real_distribution<>(-10.0, 10.0)(jdgRng)),
                                  static_cast<float>(opx.y + std::uniform_real_distribution<>(-8.0, 8.0)(jdgRng))};
                        const float angle = static_cast<float>(std::uniform_real_distribution<>(0.0, 6.283185)(jdgRng));
                        const float speed = static_cast<float>(std::uniform_real_distribution<>(30.0, 120.0)(jdgRng));
                        jp.vel = {std::cos(angle) * speed, std::sin(angle) * speed - 30.0f};
                        jp.life = 0.0f;
                        jp.maxLife = static_cast<float>(std::uniform_real_distribution<>(0.6, 1.5)(jdgRng));
                        jp.size = static_cast<float>(std::uniform_real_distribution<>(2.0, 5.0)(jdgRng));
                        judgementParticles_.push_back(jp);
                    }
                }
                // Big central ring burst
                JudgementRing jr;
                jr.radius = 30.0f;
                jr.maxRadius = 550.0f;
                jr.alpha = 1.0f;
                judgementRings_.push_back(jr);
            }

            // Continue emitting ring pulses, slower
            if (std::fmod(jElapsed, 0.22f) < 0.016f) {
                JudgementRing jr;
                jr.radius = 20.0f;
                jr.maxRadius = static_cast<float>(std::uniform_real_distribution<>(300.0, 450.0)(jdgRng));
                jr.alpha = 0.8f;
                judgementRings_.push_back(jr);
            }
        } else if (jElapsed < kFadeEnd) {
            // Phase 3: Resolution — everything fades
            const float t = (jElapsed - kCleanseEnd) / (kFadeEnd - kCleanseEnd);
            judgementLightBeamProgress_ = 1.0f - t;
            judgementDarkenAlpha_ = 80.0f * (1.0f - t);
            shakeIntensity_ = 0.0f;
        } else {
            judgementAnimPending_ = false;
            judgementLightBeamProgress_ = 0.0f;
            judgementDarkenAlpha_ = 0.0f;
            judgementParticles_.clear();
            judgementRings_.clear();
            judgementSavedObstacles_.clear();
            shakeIntensity_ = 0.0f;
        }

        // Tick particles
        for (auto& jp : judgementParticles_) {
            jp.life += 1.0f / 60.0f;
            jp.pos += jp.vel * (1.0f / 60.0f);
            jp.vel.x *= 0.98f;
        }
        judgementParticles_.erase(
            std::remove_if(judgementParticles_.begin(), judgementParticles_.end(),
                [](const JudgementParticle& jp) { return jp.life >= jp.maxLife; }),
            judgementParticles_.end());

        // Tick rings
        for (auto& jr : judgementRings_) {
            jr.radius += 180.0f / 60.0f;
            jr.alpha = std::max(0.0f, jr.alpha - 0.025f);
        }
        judgementRings_.erase(
            std::remove_if(judgementRings_.begin(), judgementRings_.end(),
                [](const JudgementRing& jr) { return jr.radius >= jr.maxRadius || jr.alpha <= 0.0f; }),
            judgementRings_.end());
    }

    // Obstacle removal animations: advance elapsed, remove completed
    for (auto it = obstacleRemovalAnims_.begin(); it != obstacleRemovalAnims_.end(); ) {
        it->elapsed += 1.0f / 60.0f;
        const float dur = it->emperorStyle ? 1.2f : 0.35f;
        if (it->elapsed >= dur) {
            board_.removeObstacleAt(it->gridPos.x, it->gridPos.y);
            it = obstacleRemovalAnims_.erase(it);
        } else {
            ++it;
        }
    }

    // Opponent sprite animation
    if (scene_ == Scene::Playing && opponentIdleFrames_ > 0) {
        OpponentAnim targetAnim = OpponentAnim::Idle;
        if (aiEnabled_ && gameOver_) {
            if (winner_ == Board::Piece::White) {
                targetAnim = OpponentAnim::Win;
            } else if (winner_ == Board::Piece::Black) {
                targetAnim = OpponentAnim::Lose;
            }
        } else if (aiEnabled_ && !gameOver_ && currentTurn_ == Board::Piece::White) {
            targetAnim = OpponentAnim::Think;
        }

        if (targetAnim != opponentAnim_) {
            opponentAnim_ = targetAnim;
            opponentLoseFinished_ = false;
            opponentAnimClock_.restart();
        }

        const int curFrames = [&] {
            switch (opponentAnim_) {
            case OpponentAnim::Think: return opponentThinkFrames_;
            case OpponentAnim::Win:   return opponentWinFrames_;
            case OpponentAnim::Lose:  return opponentLoseFrames_;
            default:                  return opponentIdleFrames_;
            }
        }();

        if (opponentAnim_ == OpponentAnim::Lose && opponentLoseFinished_) {
            // Stay on last frame
        } else {
            const float elapsed = opponentAnimClock_.getElapsedTime().asSeconds();
            const int rawFrame = static_cast<int>(elapsed / opponentFrameDuration_);
            if (opponentAnim_ == OpponentAnim::Lose && rawFrame >= curFrames) {
                opponentCurrentFrame_ = curFrames - 1;
                opponentLoseFinished_ = true;
            } else {
                opponentCurrentFrame_ = rawFrame % curFrames;
            }
        }
    }

    // Speech bubble logic
    if (scene_ == Scene::Playing && aiEnabled_ && opponentIdleFrames_ > 0 && cardEventState_ == CardEventState::Idle) {
        auto pickMsg = [&](const std::vector<std::string>& pool) -> std::string {
            if (pool.empty()) return "";
            const auto now = speechClock_.getElapsedTime().asMilliseconds();
            const int idx = static_cast<int>(now / 2500) % static_cast<int>(pool.size());
            return pool[static_cast<std::size_t>(idx)];
        };

        const auto getThinkMsgs = [&]() -> const std::vector<std::string>& {
            static const std::vector<std::string> catThink = {"嗯...走哪呢？", "喵？", "让我想想~"};
            static const std::vector<std::string> batThink = {"嘶...", "吱吱...", "看这招！"};
            static const std::vector<std::string> mageThink = {"有趣...", "呵，看穿你了。", "命运之线..."};
            if (aiDifficulty_ == AIDifficulty::Easy) return catThink;
            if (aiDifficulty_ == AIDifficulty::Medium) return batThink;
            return mageThink;
        };

        if (gameReady_) {
            speechText_.clear();
            speechIntroduced_ = false;
            speechClock_.restart();
        } else if (!speechIntroduced_ && !gameOver_ && gameIntroAlpha_ >= 1.0f) {
            // Show intro message
            speechIntroduced_ = true;
            if (aiDifficulty_ == AIDifficulty::Easy)       speechText_ = "喵~ 就凭你也想赢我？";
            else if (aiDifficulty_ == AIDifficulty::Medium) speechText_ = "吱吱...准备受死吧！";
            else                                            speechText_ = "凡人，你的败局已定。";
            speechClock_.restart();
        } else if (speechIntroduced_ && !gameOver_ && opponentAnim_ == OpponentAnim::Think) {
            // Show thinking messages
            speechText_ = pickMsg(getThinkMsgs());
        } else if (speechIntroduced_ && !gameOver_ && speechClock_.getElapsedTime().asSeconds() < 2.5f) {
            // Keep intro visible for 2.5s even during player's turn
        } else if (speechIntroduced_ && !gameOver_) {
            // Clear after intro time expires and not thinking
            speechText_.clear();
        } else if (gameOver_) {
            if (aiDifficulty_ == AIDifficulty::Easy) {
                speechText_ = (winner_ == Board::Piece::White) ? "喵哈哈，你太弱了！" : "喵...居然输了...";
            } else if (aiDifficulty_ == AIDifficulty::Medium) {
                speechText_ = (winner_ == Board::Piece::White) ? "哼，不堪一击！" : "这...怎么可能...";
            } else {
                speechText_ = (winner_ == Board::Piece::White) ? "蝼蚁岂能撼树？" : "不...这不可能！";
            }
        }
    }

    // Reset stepper animation when done — apply target value
    if (stepperAnimRow_ >= 0 && stepperAnimClock_.getElapsedTime().asSeconds() >= 0.18f) {
        if (stepperAnimRow_ == 2) roomUndoCount_ = stepperAnimTarget_;
        if (stepperAnimRow_ == 3) roomTurnTime_ = stepperAnimTarget_;
        stepperAnimRow_ = -1;
        stepperAnimDir_ = 0;
    }

    if (scene_ == Scene::MainMenu) {
        updateTitleParticles();
    } else if (scene_ != Scene::ModeSelect && scene_ != Scene::DifficultySelect && scene_ != Scene::Playing && scene_ != Scene::NetworkLobby && scene_ != Scene::RoomSetup && scene_ != Scene::NetworkWait) {
        buttonAnimIndex_ = -1;
    }

    // Button click animation (main menu: 0-2, mode select: 100-104, difficulty: 200-203, game: 300-313)
    if (buttonAnimIndex_ >= 0 && buttonAnimClock_.getElapsedTime().asMilliseconds() >= 160) {
        if (buttonAnimIndex_ < 10) {
            switch (buttonAnimIndex_) {
            case 0:
                scene_ = Scene::ModeSelect;
                updateWindowTitle();
                break;
            case 1:
                scene_ = Scene::Rules;
                updateWindowTitle();
                break;
            case 2:
                window_.close();
                return;
            }
        } else {
            switch (buttonAnimIndex_) {
            case 100:
                boardMode_ = Board::Mode::Classic;
                scene_ = Scene::DifficultySelect;
                updateWindowTitle();
                break;
            case 101:
                boardMode_ = Board::Mode::Obstacle;
                scene_ = Scene::DifficultySelect;
                updateWindowTitle();
                break;
            case 102:
                startTransition([this] { startMatch(Board::Mode::Classic, false, AIDifficulty::Medium); });
                break;
            case 103:
                for (const auto* prefix : {"assets/", "../assets/"}) {
                    if (std::filesystem::exists(std::string(prefix) + std::string("222.png"))) {
                        static_cast<void>(networkBgTex_.loadFromFile(std::string(prefix) + std::string("222.png")));
                        networkBgTex_.setSmooth(false);
                        break;
                    }
                }
                modeSelectClock_.restart();
                scene_ = Scene::NetworkLobby;
                updateWindowTitle();
                break;
            case 104:
                scene_ = Scene::MainMenu;
                updateWindowTitle();
                break;
            case 200:
                startTransition([this] { startMatch(boardMode_, true, AIDifficulty::Easy); });
                break;
            case 201:
                startTransition([this] { startMatch(boardMode_, true, AIDifficulty::Medium); });
                break;
            case 202:
                startTransition([this] { startMatch(boardMode_, true, AIDifficulty::Hard); });
                break;
            case 203:
                scene_ = Scene::ModeSelect;
                updateWindowTitle();
                break;
            case 400:
                modeSelectClock_.restart();
                scene_ = Scene::RoomSetup;
                updateWindowTitle();
                break;
            case 401:
                joinIp_.clear();
                ipInputActive_ = false;
                scene_ = Scene::NetworkWait;
                modeSelectClock_.restart();
                waitParticles_.clear();
                updateWindowTitle();
                break;
            case 402:
                scene_ = Scene::ModeSelect;
                updateWindowTitle();
                break;
            case 420:
                startNetworkHost();
                break;
            case 421:
                scene_ = Scene::NetworkLobby;
                modeSelectClock_.restart();
                updateWindowTitle();
                break;
            case 403:
                stopNetwork();
                scene_ = Scene::NetworkLobby;
                modeSelectClock_.restart();
                updateWindowTitle();
                break;
            case 404:
                ipInputActive_ = false;
                startNetworkJoin();
                break;
            // Game scene buttons (non-network: 300-302, network: 310-313)
            case 300:
                if (!gameOver_ && (!aiEnabled_ || remainingUndos_ != 0)) undoMove();
                break;
            case 301:
                restart();
                break;
            case 302:
                startTransition([this] { returnToMainMenu(); });
                break;
            case 310:
                startTransition([this] { stopNetwork(); returnToMainMenu(); });
                break;
            case 311:
                if (server_) server_->sendRestart();
                else if (client_) client_->sendRestart();
                restart();
                if (server_ && boardMode_ == Board::Mode::Obstacle) {
                    server_->sendObstacleSync(board_.obstaclePositions());
                }
                break;
            case 312:
                if (remainingUndos_ > 0 && !gameOver_ && !isMyTurn()) undoNetworkMove();
                break;
            case 313:
                if (!gameOver_) surrender();
                break;
            }
        }
        buttonAnimIndex_ = -1;
    }

    processNetworkEvents();

    if (obstacleEventPending_ && obstacleEventClock_.getElapsedTime().asMilliseconds() >= 400) {
        obstacleEventPending_ = false;

        if (pendingDespawnAmount_ > 0) {
            board_.removeOldestObstacles(pendingDespawnAmount_);
        }
        if (pendingSpawnAmount_ > 0) {
            board_.spawnSmartObstacles(pendingSpawnAmount_);
        }
        pendingSpawnAmount_ = 0;
        pendingDespawnAmount_ = 0;
        updateWindowTitle();

        if (networkMode_ && isNetworkHost_ && server_) {
            server_->sendObstacleSync(board_.obstaclePositions());
        }

        if (aiEnabled_ && !gameOver_ && currentTurn_ == Board::Piece::White) {
            aiMovePending_ = true;
            const int roll = std::rand() % 100;
            if (roll < 15)       aiThinkTimeMs_ = 150 + std::rand() % 350;
            else if (roll < 85)  aiThinkTimeMs_ = 700 + std::rand() % 1800;
            else                 aiThinkTimeMs_ = 2000 + std::rand() % 1500;
            aiClock_.restart();
        }
    }

    if (aiMovePending_ && !deathAnimPending_ && !loversAnimPending_ && !worldAnimPending_ && !empressAnimPending_ && !highPriestessAnimPending_ && !sunAnimPending_ && !moonAnimPending_ && !hermitAnimPending_ && !hierophantAnimPending_ && !justiceAnimPending_ && !hangedManAnimPending_ && !devilAnimPending_ && !chariotAnimPending_ && !magicianAnimPending_ && !temperanceAnimPending_ && !foolAnimPending_ && !towerAnimPending_ && !wheelFortuneAnimPending_ && !judgementAnimPending_ && (!starAnimPending_ || starInPersistentMode_) && aiClock_.getElapsedTime().asMilliseconds() >= aiThinkTimeMs_) {
        aiMovePending_ = false;
        maybeMakeAiMove();
        updateWindowTitle();
    }

    board_.clearExpiredObstacleAnimations();
    updateGameParticles();
    updateSparks();

    // Turn countdown timer — only active player triggers auto-place in network mode
    if (scene_ == Scene::Playing && !gameReady_ && !gameOver_ && transitionState_ == TransitionState::None &&
        !obstacleEventPending_ && !turnTimedOut_ && !deathAnimPending_ && !loversAnimPending_ && !worldAnimPending_ && !empressAnimPending_ && !highPriestessAnimPending_ && !sunAnimPending_ && !moonAnimPending_ && !hermitAnimPending_ && !hierophantAnimPending_ && !justiceAnimPending_ && !hangedManAnimPending_ && !devilAnimPending_ && !chariotAnimPending_ && !magicianAnimPending_ && !temperanceAnimPending_ && !foolAnimPending_ && !towerAnimPending_ && !wheelFortuneAnimPending_ && !judgementAnimPending_ && (!starAnimPending_ || starInPersistentMode_) && cardEventState_ == CardEventState::Idle) {
        const int effectiveTime = hermitRemaining_ > 0 ? turnTimeLimit_ * 2 : turnTimeLimit_;
        if ((!networkMode_ || isMyTurn()) &&
            turnTimer_.getElapsedTime().asSeconds() >= static_cast<float>(effectiveTime)) {
            turnTimedOut_ = true;
            autoPlaceRandom();
        }
    }
}

void Game::render() {
    window_.clear(sf::Color(14, 24, 43));

    switch (scene_) {
    case Scene::MainMenu:
        drawMainMenu();
        break;
    case Scene::Rules:
        drawRulesScene();
        break;
    case Scene::ModeSelect:
        drawModeSelectScene();
        break;
    case Scene::DifficultySelect:
        drawDifficultySelectScene();
        break;
    case Scene::NetworkLobby:
        drawNetworkLobbyScene();
        break;
    case Scene::RoomSetup:
        drawRoomSetupScene();
        break;
    case Scene::NetworkWait:
        drawNetworkWaitScene();
        break;
    case Scene::Playing:
        drawGameScene();
        break;
    }

    // Transition overlay (fade to black + pixel dissolve)
    if (transitionState_ != TransitionState::None) {
        const float t = transitionAlpha_;
        // Smooth fade base
        sf::RectangleShape fade;
        fade.setPosition({0.0f, 0.0f});
        fade.setSize({1280.0f, 820.0f});
        fade.setFillColor(sf::Color(0, 0, 0, static_cast<std::uint8_t>(t * 200.0f)));
        window_.draw(fade);
        // Pixel dissolve grain via noise texture
        if (noiseTex_.getSize().x > 0) {
            sf::Sprite ns(noiseTex_);
            ns.setTextureRect(sf::IntRect({0, 0}, {1280, 820}));
            ns.setPosition({0.0f, 0.0f});
            ns.setScale({1280.0f / static_cast<float>(noiseTex_.getSize().x),
                         820.0f / static_cast<float>(noiseTex_.getSize().y)});
            ns.setColor(sf::Color(0, 0, 0, static_cast<std::uint8_t>(t * t * 180.0f)));
            window_.draw(ns);
        }
    }

    window_.display();
}

void Game::startMatch(Board::Mode mode, bool aiEnabled, AIDifficulty difficulty) {
    boardMode_ = mode;
    aiEnabled_ = aiEnabled;
    aiDifficulty_ = difficulty;
    obstacleDynamic_ = (difficulty == AIDifficulty::Hard);
    scene_ = Scene::Playing;
    gameIntroAlpha_ = 0.0f;
    gameIntroClock_.restart();
    gameParticles_.clear();
    gameParticleClock_.restart();
    gameParticleTimer_ = 0.0f;
    atmosphereClock_.restart();
    sparks_.clear();
    gameReady_ = true;
    winLoseTriggered_ = false;

    // Reset card event system
    nextCardThreshold_ = 10;
    cardEventState_ = CardEventState::Idle;
    lastCardEventState_ = CardEventState::Idle;
    selectedCardIndex_ = -1;
    foolActive_ = false;
    hierophantRemaining_ = 0;
    temperanceRemaining_ = 0;
    strengthProtectionRemaining_ = 0;
    strengthProtectedPos_ = {-1, -1};
    undoUsed_ = false;
    moonActive_ = false;
    starHighlightValid_ = false;
    cardDeferredTurn_ = false;
    chariotPending_ = false;
    hermitRemaining_ = 0;
    iAmPicker_ = false;
    cardEffectSeed_ = 0;
    cardEffectSeedSet_ = false;
    deathAnimPending_ = false;
    deathCenter_ = {-1, -1};
    deathSpreadStep_ = 0;
    deathFlashAlpha_ = 0.0f;
    deathConsumeProgress_ = 0.0f;
    deathPieces_.clear();
    loversAnimPending_ = false;
    loversHeartParticles_.clear();
    loversArcPathA_.clear();
    loversArcPathB_.clear();
    worldAnimPending_ = false;
    worldParticles_.clear();
    worldSealTriggered_ = false;
    ringRadii_ = {};
    ringAlphas_ = {};
    strengthAnimPending_ = false;
    strengthParticles_.clear();
    strengthFlashAlpha_ = 0.0f;
    strengthShockwaveRadius_ = 0.0f;
    strengthShockwaveAlpha_ = 0.0f;
    empressAnimPending_ = false;
    empressPiecePlaced_ = false;
    empressPetals_.clear();
    empressFireflies_.clear();
    highPriestessAnimPending_ = false;
    hpPiecesRemoved_ = false;
    hpMotes_.clear();
    hpDissolvingPieces_.clear();
    sunAnimPending_ = false;
    sunMotes_.clear();
    starAnimPending_ = false;
    starInPersistentMode_ = false;
    starDustParticles_.clear();
    moonAnimPending_ = false;
    moonMistParticles_.clear();
    moonOriginalPos_ = {-1, -1};
    moonMarkerAlpha_ = 0.0f;
    hermitAnimPending_ = false;
    hermitLanterns_.clear();
    hermitRipples_.clear();
    hierophantAnimPending_ = false;
    hierophantParticles_.clear();
    hierophantCornerAlpha_ = 0.0f;
    justiceAnimPending_ = false;
    justiceFeathers_.clear();
    justicePiecesRemoved_ = false;
    justiceBlackPiece_ = {-1, -1};
    justiceWhitePiece_ = {-1, -1};
    hangedManAnimPending_ = false;
    hangedManParticles_.clear();
    hangedManSacrificed_ = false;
    hangedManTargetsRemoved_ = false;
    hangedManSacrificePos_ = {-1, -1};
    hangedManTargetA_ = {-1, -1};
    hangedManTargetB_ = {-1, -1};
    devilAnimPending_ = false;
    devilParticles_.clear();
    devilPieceRemoved_ = false;
    devilTargetPos_ = {-1, -1};
    chariotAnimPending_ = false;
    chariotParticles_.clear();
    chariotPiecePushed_ = false;
    chariotPiecePlaced_ = false;
    chariotPushSource_ = {-1, -1};
    chariotPushDest_ = {-1, -1};
    chariotPlayerPos_ = {-1, -1};
    magicianAnimPending_ = false;
    magicianParticles_.clear();
    magicianPiecePlaced_ = false;
    magicianSourcePos_ = {-1, -1};
    magicianTargetPos_ = {-1, -1};
    temperanceAnimPending_ = false;
    temperanceParticles_.clear();
    temperanceMarkAlpha_ = 0.0f;
    foolAnimPending_ = false;
    foolParticles_.clear();
    foolStarAlpha_ = 0.0f;
    towerAnimPending_ = false;
    towerParticles_.clear();
    towerSavedObstacles_.clear();
    towerObstaclesCleared_ = false;
    towerObstaclesRegenerated_ = false;
    wheelFortuneAnimPending_ = false;
    wheelFortuneParticles_.clear();
    wheelFortuneSavedObstacles_.clear();
    wheelFortuneObstacleCount_ = 0;
    wheelFortuneAngle_ = 0.0f;
    wheelFortuneAlpha_ = 0.0f;
    wheelFortuneRadius_ = 0.0f;
    wheelFortuneOrbProgress_ = 0.0f;
    wheelFortuneObstaclesCleared_ = false;
    wheelFortuneObstaclesRegenerated_ = false;
    judgementAnimPending_ = false;
    judgementParticles_.clear();
    judgementRings_.clear();
    judgementSavedObstacles_.clear();
    judgementLightBeamProgress_ = 0.0f;
    judgementDarkenAlpha_ = 0.0f;
    judgementObstaclesCleared_ = false;
    obstacleRemovalAnims_.clear();
    shakeIntensity_ = 0.0f;
    darkenAlpha_ = 0.0f;
    messengerAlpha_ = 0.0f;
    cardDealProgress_ = 0.0f;
    cardFlipProgress_ = 0.0f;
    cardFloatTime_ = 0.0f;
    cardRevealProgress_ = 0.0f;
    chosenCardIdx_ = -1;
    cardEventSpeech_.clear();
    if (mode == Board::Mode::Obstacle && cardTextures_[0].getSize().x == 0) {
        loadCardTextures();
    }

    // Load background: selected map > difficulty-based > default
    bool bgLoaded = false;
    if (selectedMapIndex_ >= 0 && selectedMapIndex_ < static_cast<int>(mapFiles_.size())) {
        if (std::filesystem::exists(mapFiles_[selectedMapIndex_])) {
            static_cast<void>(gameBgTex_.loadFromFile(mapFiles_[selectedMapIndex_]));
            gameBgTex_.setSmooth(false);
            bgLoaded = true;
        }
    }
    if (!bgLoaded) {
        const char* bgFile = "222.png";
        if (aiEnabled) {
            switch (difficulty) {
            case AIDifficulty::Easy: bgFile = "33.png"; break;
            case AIDifficulty::Medium: bgFile = "111.png"; break;
            case AIDifficulty::Hard: bgFile = "444.png"; break;
            }
        }
        for (const auto* prefix : {"assets/", "../assets/"}) {
            std::string path = std::string(prefix) + bgFile;
            if (std::filesystem::exists(path)) {
                static_cast<void>(gameBgTex_.loadFromFile(path));
                gameBgTex_.setSmooth(false);
                break;
            }
        }
    }

    // Load opponent character sprite (AI mode only)
    if (aiEnabled) {
        const char* idleFile = nullptr;
        const char* thinkFile = nullptr;
        const char* winFile = nullptr;
        const char* loseFile = nullptr;
        int idleFrames = 0;

        if (difficulty == AIDifficulty::Easy) {
            idleFile = "Cat_idle.png";
            thinkFile = "Cat_think.png";
            winFile = "Cat_win.png";
            loseFile = "Cat_lose.png";
            idleFrames = 3;
            opponentScale_ = 3.5f;
            opponentOffsetX_ = -20.0f;
        } else if (difficulty == AIDifficulty::Medium) {
            idleFile = "Bat-IdleFly.png";
            thinkFile = "Bat-think.png";
            winFile = "Bat-win.png";
            loseFile = "Bat-Lose.png";
            idleFrames = 9;
            opponentScale_ = 3.0f;
            opponentOffsetX_ = 0.0f;
        } else if (difficulty == AIDifficulty::Hard) {
            idleFile = "forrest_mage_idle.png";
            thinkFile = "forrest_mage_think.png";
            winFile = "forrest_mage_win.png";
            loseFile = "forrest_mage_lose.png";
            idleFrames = 6;
            opponentScale_ = 2.5f;
            opponentOffsetX_ = 0.0f;
        }

        if (idleFile) {
            for (const auto* prefix : {"assets/", "../assets/"}) {
                const std::string path = std::string(prefix) + idleFile;
                if (std::filesystem::exists(path)) {
                    if (opponentTex_.loadFromFile(path)) {
                        opponentTex_.setSmooth(false);
                        const auto sz = opponentTex_.getSize();
                        opponentIdleFrames_ = idleFrames;
                        opponentFrameWidth_ = static_cast<int>(sz.x) / opponentIdleFrames_;
                        opponentFrameHeight_ = static_cast<int>(sz.y);
                        opponentCurrentFrame_ = 0;
                        opponentAnim_ = OpponentAnim::Idle;
                        opponentLoseFinished_ = false;
                        opponentAnimClock_.restart();
                    }
                    break;
                }
            }
        }
        if (thinkFile) {
            for (const auto* prefix : {"assets/", "../assets/"}) {
                const std::string path = std::string(prefix) + thinkFile;
                if (std::filesystem::exists(path)) {
                    static_cast<void>(opponentThinkTex_.loadFromFile(path));
                    opponentThinkTex_.setSmooth(false);
                    opponentThinkFrames_ = static_cast<int>(opponentThinkTex_.getSize().x) / opponentFrameWidth_;
                    break;
                }
            }
        }
        if (winFile) {
            for (const auto* prefix : {"assets/", "../assets/"}) {
                const std::string path = std::string(prefix) + winFile;
                if (std::filesystem::exists(path)) {
                    static_cast<void>(opponentWinTex_.loadFromFile(path));
                    opponentWinTex_.setSmooth(false);
                    opponentWinFrames_ = static_cast<int>(opponentWinTex_.getSize().x) / opponentFrameWidth_;
                    break;
                }
            }
        }
        if (loseFile) {
            for (const auto* prefix : {"assets/", "../assets/"}) {
                const std::string path = std::string(prefix) + loseFile;
                if (std::filesystem::exists(path)) {
                    static_cast<void>(opponentLoseTex_.loadFromFile(path));
                    opponentLoseTex_.setSmooth(false);
                    opponentLoseFrames_ = static_cast<int>(opponentLoseTex_.getSize().x) / opponentFrameWidth_;
                    break;
                }
            }
        }
    }

    restart();
}

void Game::returnToMainMenu() {
    scene_ = Scene::MainMenu;
    updateWindowTitle();
}

void Game::startTransition(std::function<void()> action) {
    transitionState_ = TransitionState::FadingOut;
    transitionAlpha_ = 0.0f;
    transitionAction_ = std::move(action);
    transitionClock_.restart();
}

void Game::restart() {
    board_.reset(boardMode_);
    hardModeObstacleMoves_ = 0;

    if (aiEnabled_) {
        switch (aiDifficulty_) {
        case AIDifficulty::Easy:   remainingUndos_ = 3; break;
        case AIDifficulty::Medium: remainingUndos_ = 2; break;
        case AIDifficulty::Hard:   remainingUndos_ = 1; break;
        }
    } else if (networkMode_) {
        remainingUndos_ = roomUndoCount_;
        turnTimeLimit_ = roomTurnTime_;
    } else {
        remainingUndos_ = -1;
    }

    if (boardMode_ == Board::Mode::Obstacle) {
        if (cardTextures_[0].getSize().x == 0) {
            loadCardTextures();
        }
        if (networkMode_ && !isNetworkHost_) {
            // Client waits for obstacle sync from host
            board_.clearObstacles();
        } else if (obstacleDynamic_) {
            board_.clearObstacles();
        } else {
            board_.generateRandomObstacles(8);
        }
    }

    currentTurn_ = Board::Piece::Black;
    winner_ = Board::Piece::None;
    gameOver_ = false;
    winLoseTriggered_ = false;
    aiMovePending_ = false;
    obstacleEventPending_ = false;
    turnTimedOut_ = false;
    turnTimer_.restart();

    // Reset card event system
    nextCardThreshold_ = 10;
    cardEventState_ = CardEventState::Idle;
    lastCardEventState_ = CardEventState::Idle;
    selectedCardIndex_ = -1;
    foolActive_ = false;
    hierophantRemaining_ = 0;
    temperanceRemaining_ = 0;
    strengthProtectionRemaining_ = 0;
    strengthProtectedPos_ = {-1, -1};
    undoUsed_ = false;
    moonActive_ = false;
    starHighlightValid_ = false;
    cardDeferredTurn_ = false;
    chariotPending_ = false;
    hermitRemaining_ = 0;
    iAmPicker_ = false;
    cardEffectSeed_ = 0;
    cardEffectSeedSet_ = false;
    deathAnimPending_ = false;
    deathCenter_ = {-1, -1};
    deathSpreadStep_ = 0;
    deathFlashAlpha_ = 0.0f;
    deathConsumeProgress_ = 0.0f;
    deathPieces_.clear();
    loversAnimPending_ = false;
    loversHeartParticles_.clear();
    loversArcPathA_.clear();
    loversArcPathB_.clear();
    worldAnimPending_ = false;
    worldParticles_.clear();
    worldSealTriggered_ = false;
    ringRadii_ = {};
    ringAlphas_ = {};
    strengthAnimPending_ = false;
    strengthParticles_.clear();
    strengthFlashAlpha_ = 0.0f;
    strengthShockwaveRadius_ = 0.0f;
    strengthShockwaveAlpha_ = 0.0f;
    empressAnimPending_ = false;
    empressPiecePlaced_ = false;
    empressPetals_.clear();
    empressFireflies_.clear();
    highPriestessAnimPending_ = false;
    hpPiecesRemoved_ = false;
    hpMotes_.clear();
    hpDissolvingPieces_.clear();
    sunAnimPending_ = false;
    sunMotes_.clear();
    starAnimPending_ = false;
    starInPersistentMode_ = false;
    starDustParticles_.clear();
    moonAnimPending_ = false;
    moonMistParticles_.clear();
    moonOriginalPos_ = {-1, -1};
    moonMarkerAlpha_ = 0.0f;
    hermitAnimPending_ = false;
    hermitLanterns_.clear();
    hermitRipples_.clear();
    hierophantAnimPending_ = false;
    hierophantParticles_.clear();
    hierophantCornerAlpha_ = 0.0f;
    justiceAnimPending_ = false;
    justiceFeathers_.clear();
    justicePiecesRemoved_ = false;
    justiceBlackPiece_ = {-1, -1};
    justiceWhitePiece_ = {-1, -1};
    hangedManAnimPending_ = false;
    hangedManParticles_.clear();
    hangedManSacrificed_ = false;
    hangedManTargetsRemoved_ = false;
    hangedManSacrificePos_ = {-1, -1};
    hangedManTargetA_ = {-1, -1};
    hangedManTargetB_ = {-1, -1};
    devilAnimPending_ = false;
    devilParticles_.clear();
    devilPieceRemoved_ = false;
    devilTargetPos_ = {-1, -1};
    chariotAnimPending_ = false;
    chariotParticles_.clear();
    chariotPiecePushed_ = false;
    chariotPiecePlaced_ = false;
    chariotPushSource_ = {-1, -1};
    chariotPushDest_ = {-1, -1};
    chariotPlayerPos_ = {-1, -1};
    magicianAnimPending_ = false;
    magicianParticles_.clear();
    magicianPiecePlaced_ = false;
    magicianSourcePos_ = {-1, -1};
    magicianTargetPos_ = {-1, -1};
    temperanceAnimPending_ = false;
    temperanceParticles_.clear();
    temperanceMarkAlpha_ = 0.0f;
    foolAnimPending_ = false;
    foolParticles_.clear();
    foolStarAlpha_ = 0.0f;
    towerAnimPending_ = false;
    towerParticles_.clear();
    towerSavedObstacles_.clear();
    towerObstaclesCleared_ = false;
    towerObstaclesRegenerated_ = false;
    wheelFortuneAnimPending_ = false;
    wheelFortuneParticles_.clear();
    wheelFortuneSavedObstacles_.clear();
    wheelFortuneObstacleCount_ = 0;
    wheelFortuneAngle_ = 0.0f;
    wheelFortuneAlpha_ = 0.0f;
    wheelFortuneRadius_ = 0.0f;
    wheelFortuneOrbProgress_ = 0.0f;
    wheelFortuneObstaclesCleared_ = false;
    wheelFortuneObstaclesRegenerated_ = false;
    judgementAnimPending_ = false;
    judgementParticles_.clear();
    judgementRings_.clear();
    judgementSavedObstacles_.clear();
    judgementLightBeamProgress_ = 0.0f;
    judgementDarkenAlpha_ = 0.0f;
    judgementObstaclesCleared_ = false;
    obstacleRemovalAnims_.clear();
    shakeIntensity_ = 0.0f;
    darkenAlpha_ = 0.0f;
    messengerAlpha_ = 0.0f;
    cardDealProgress_ = 0.0f;
    cardFlipProgress_ = 0.0f;
    cardFloatTime_ = 0.0f;
    cardRevealProgress_ = 0.0f;
    chosenCardIdx_ = -1;
    cardEventSpeech_.clear();

    updateWindowTitle();
}

void Game::undoMove() {
    if (gameOver_ || turnTimedOut_ || deathAnimPending_ || loversAnimPending_ || worldAnimPending_ || strengthAnimPending_ || empressAnimPending_ || highPriestessAnimPending_ || sunAnimPending_ || moonAnimPending_ || hermitAnimPending_ || hierophantAnimPending_ || justiceAnimPending_ || hangedManAnimPending_ || devilAnimPending_ || chariotAnimPending_ || magicianAnimPending_ || temperanceAnimPending_ || foolAnimPending_ || towerAnimPending_ || wheelFortuneAnimPending_ || judgementAnimPending_ || (starAnimPending_ && !starInPersistentMode_) || remainingUndos_ == 0 || undoUsed_) {
        return;
    }

    const auto lastMove = board_.undoLastMove();
    if (!lastMove.has_value()) {
        return;
    }

    if (remainingUndos_ > 0) {
        --remainingUndos_;
    }

    currentTurn_ = lastMove->piece;
    if (aiEnabled_ && lastMove->piece == Board::Piece::White) {
        const auto playerMove = board_.undoLastMove();
        if (playerMove.has_value()) {
            currentTurn_ = playerMove->piece;
        }
    }

    undoUsed_ = true;
    winner_ = Board::Piece::None;
    gameOver_ = false;
    aiMovePending_ = false;
    obstacleEventPending_ = false;
    turnTimedOut_ = false;
    turnTimer_.restart();
    updateWindowTitle();
}

void Game::undoNetworkMove() {
    if (remainingUndos_ <= 0 || gameOver_ || deathAnimPending_ || loversAnimPending_ || worldAnimPending_ || strengthAnimPending_ || empressAnimPending_ || highPriestessAnimPending_ || sunAnimPending_ || moonAnimPending_ || hermitAnimPending_ || hierophantAnimPending_ || justiceAnimPending_ || hangedManAnimPending_ || devilAnimPending_ || chariotAnimPending_ || magicianAnimPending_ || temperanceAnimPending_ || foolAnimPending_ || towerAnimPending_ || wheelFortuneAnimPending_ || judgementAnimPending_ || (starAnimPending_ && !starInPersistentMode_) || isMyTurn() || undoUsed_) {
        return;
    }

    const auto lastMove = board_.undoLastMove();
    if (!lastMove.has_value()) {
        return;
    }

    --remainingUndos_;
    undoUsed_ = true;
    currentTurn_ = lastMove->piece;
    winner_ = Board::Piece::None;
    gameOver_ = false;
    aiMovePending_ = false;
    obstacleEventPending_ = false;

    if (server_) {
        server_->sendUndo();
    } else if (client_) {
        client_->sendUndo();
    }
    updateWindowTitle();
}

void Game::surrender() {
    if (gameOver_ || deathAnimPending_ || loversAnimPending_ || worldAnimPending_ || strengthAnimPending_ || empressAnimPending_ || highPriestessAnimPending_ || sunAnimPending_ || moonAnimPending_ || hermitAnimPending_ || hierophantAnimPending_ || justiceAnimPending_ || hangedManAnimPending_ || devilAnimPending_ || chariotAnimPending_ || magicianAnimPending_ || temperanceAnimPending_ || foolAnimPending_ || towerAnimPending_ || wheelFortuneAnimPending_ || judgementAnimPending_ || (starAnimPending_ && !starInPersistentMode_)) {
        return;
    }
    gameOver_ = true;
    winner_ = isNetworkHost_ ? Board::Piece::White : Board::Piece::Black;

    if (server_) {
        server_->sendSurrender();
    } else if (client_) {
        client_->sendSurrender();
    }
    updateWindowTitle();
}

void Game::tryPlacePiece(sf::Vector2i pixel) {
    if (scene_ != Scene::Playing || gameReady_ || gameOver_ || obstacleEventPending_ || deathAnimPending_ || loversAnimPending_ || worldAnimPending_ || empressAnimPending_ || highPriestessAnimPending_ || sunAnimPending_ || moonAnimPending_ || hermitAnimPending_ || hierophantAnimPending_ || justiceAnimPending_ || hangedManAnimPending_ || devilAnimPending_ || chariotAnimPending_ || magicianAnimPending_ || temperanceAnimPending_ || foolAnimPending_ || towerAnimPending_ || wheelFortuneAnimPending_ || judgementAnimPending_ || (starAnimPending_ && !starInPersistentMode_)) {
        return;
    }
    if (cardEventState_ != CardEventState::Idle) {
        return;
    }
    if (networkMode_ && !isMyTurn()) {
        return;
    }
    if (!networkMode_ && aiEnabled_ && currentTurn_ == Board::Piece::White) {
        return;
    }

    // Hierophant restriction: center 9x9 only
    if (hierophantRemaining_ > 0) {
        const auto cell = board_.pixelToCell(pixel);
        if (!cell.has_value()) { return; }
        if (cell->x < 3 || cell->x > 11 || cell->y < 3 || cell->y > 11) {
            return;
        }
    }

    // Chariot: push opponent piece away if target cell is occupied
    if (chariotPending_) {
        chariotPending_ = false;
        const auto chariotCell = board_.pixelToCell(pixel);
        if (chariotCell.has_value() && board_.pieceAt(chariotCell->x, chariotCell->y) == nextTurn(currentTurn_)) {
            constexpr int kPushDirs[8][2] = {{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}};
            bool found = false;
            sf::Vector2i dest;
            for (const auto& d : kPushDirs) {
                const int nr = chariotCell->x + d[0];
                const int nc = chariotCell->y + d[1];
                if (nr >= 0 && nr < Board::kBoardSize && nc >= 0 && nc < Board::kBoardSize &&
                    board_.canPlaceAt(nr, nc)) {
                    dest = {nr, nc};
                    found = true;
                    break;
                }
            }
            if (!found) return; // can't push — no adjacent empty cell

            // Defer: charge animation then push+place
            chariotPushSource_ = *chariotCell;
            chariotPushDest_ = dest;
            chariotPlayerPos_ = *chariotCell;
            if (!chariotTexturesLoaded_) generateChariotTextures();
            chariotAnimPending_ = true;
            chariotAnimClock_.restart();
            chariotOverlayAlpha_ = 0.0f;
            chariotChargeAlpha_ = 0.0f;
            chariotGearAngle_ = 0.0f;
            chariotImpactAlpha_ = 0.0f;
            chariotTrailAlpha_ = 0.0f;
            chariotPiecePushed_ = false;
            chariotPiecePlaced_ = false;
            chariotParticles_.clear();
            return; // Defer board ops and player placement to animation
        }
        // Target not opponent piece — chariot consumed without effect, fall through
    }

    // Moon effect: offset this move by 1 random adjacent cell
    // In network PvP: the card drawer's opponent gets their move randomly shifted
    if (networkMode_ && moonActive_ && !iAmPicker_) {
        moonActive_ = false;
        if (auto cell = board_.pixelToCell(pixel); cell.has_value()) {
            moonOriginalPos_ = *cell;
            moonMarkerAlpha_ = 1.0f;
            static constexpr int kOffsets[8][2] = {{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}};
            static thread_local std::mt19937 rng(std::random_device{}());
            const auto& off = kOffsets[std::uniform_int_distribution<>(0, 7)(rng)];
            const int nr = cell->x + off[0];
            const int nc = cell->y + off[1];
            if (nr >= 0 && nr < Board::kBoardSize && nc >= 0 && nc < Board::kBoardSize &&
                board_.canPlaceAt(nr, nc)) {
                pixel = sf::Vector2i(board_.cellToPixel(nr, nc));
            }
        }
    }

    if (!board_.placePieceFromPixel(pixel, currentTurn_)) {
        return;
    }

    playPlaceSound();

    // Spawn pixel sparks at placed position
    if (const auto pos = board_.lastMovePosition(); pos.has_value()) {
        spawnSparks(board_.cellToPixel(pos->x, pos->y), currentTurn_);
    }

    // Clear Star highlight on placement
    starHighlightValid_ = false;

    // Track Strength-empowered piece — counts as 2 for winning
    // Only infuse the FIRST piece placed after drawing Strength; buff stays there for 3 turns
    if (strengthProtectionRemaining_ > 0 && strengthProtectedPos_.x < 0) {
        if (const auto pos = board_.lastMovePosition(); pos.has_value()) {
            strengthProtectedPos_ = *pos;
            board_.setStrengthPos(*pos);
            // Trigger power infusion animation (private: only drawer sees it)
            if (!networkMode_ || iAmPicker_) {
                if (!strengthTexturesLoaded_) generateStrengthTextures();
                strengthAnimPending_ = true;
                strengthAnimClock_.restart();
                strengthFlashAlpha_ = 0.0f;
                strengthShockwaveRadius_ = 0.0f;
                strengthShockwaveAlpha_ = 0.0f;
                strengthParticles_.clear();
            }
        }
    }

    turnTimedOut_ = false;
    undoUsed_ = false;
    turnTimer_.restart();

    winner_ = board_.winnerFromLastMove();
    if (winner_ != Board::Piece::None) {
        gameOver_ = true;
    } else if (!board_.hasEmptyCell()) {
        gameOver_ = true;
    }

    // Check card event trigger BEFORE switching turn
    bool cardTriggered = false;
    if (!gameOver_ && cardEventState_ == CardEventState::Idle) {
        const int totalPieces = board_.totalPieceCount();
        if (totalPieces >= nextCardThreshold_) {
            triggerCardEvent();
            nextCardThreshold_ += 8;
            cardTriggered = true;
        }
    }

    // Only switch turn if card event didn't fire (otherwise deferred to after card)
    if (!cardTriggered && !gameOver_) {
        currentTurn_ = nextTurn(currentTurn_);
    } else if (cardTriggered) {
        cardDeferredTurn_ = true;
    }

    // Decrement turn-based card effects after human places
    if (!gameOver_ && hierophantRemaining_ > 0) --hierophantRemaining_;
    if (!gameOver_ && hermitRemaining_ > 0) --hermitRemaining_;
    if (!gameOver_ && temperanceRemaining_ > 0) {
        --temperanceRemaining_;
        if (temperanceRemaining_ == 0) board_.setTemperanceActive(false);
    }

    updateHardModeObstacles();

    if (networkMode_) {
        const auto lastMove = board_.lastMovePosition();
        if (lastMove.has_value()) {
            if (server_) {
                server_->sendMove(lastMove->x, lastMove->y);
            } else if (client_) {
                client_->sendMove(lastMove->x, lastMove->y);
            }
        }
    }

    if (!obstacleEventPending_ && cardEventState_ == CardEventState::Idle && aiEnabled_ && !gameOver_ && currentTurn_ == Board::Piece::White) {
        aiMovePending_ = true;
        const int roll = std::rand() % 100;
        if (roll < 15)       aiThinkTimeMs_ = 150 + std::rand() % 350;
        else if (roll < 85)  aiThinkTimeMs_ = 700 + std::rand() % 1800;
        else                 aiThinkTimeMs_ = 2000 + std::rand() % 1500;
        aiClock_.restart();
    }
    updateWindowTitle();
}

void Game::maybeMakeAiMove() {
    if (!aiEnabled_ || gameOver_ || deathAnimPending_ || loversAnimPending_ || worldAnimPending_ || empressAnimPending_ || highPriestessAnimPending_ || sunAnimPending_ || moonAnimPending_ || hermitAnimPending_ || hierophantAnimPending_ || justiceAnimPending_ || hangedManAnimPending_ || devilAnimPending_ || chariotAnimPending_ || magicianAnimPending_ || temperanceAnimPending_ || foolAnimPending_ || towerAnimPending_ || wheelFortuneAnimPending_ || judgementAnimPending_ || (starAnimPending_ && !starInPersistentMode_) || currentTurn_ != Board::Piece::White) {
        return;
    }
    if (cardEventState_ != CardEventState::Idle) {
        return;
    }

    std::optional<sf::Vector2i> bestMove;
    switch (aiDifficulty_) {
    case AIDifficulty::Easy:
        bestMove = findEasyAiMove();
        break;
    case AIDifficulty::Hard:
        bestMove = findHardAiMove();
        break;
    case AIDifficulty::Medium:
    default:
        bestMove = findBestAiMove();
        break;
    }
    if (!bestMove.has_value()) {
        gameOver_ = true;
        return;
    }

    // Moon effect: offset opponent's next placement by 1 random cell
    if (moonActive_) {
        moonActive_ = false;
        moonOriginalPos_ = *bestMove;  // Mark where AI originally wanted to place
        moonMarkerAlpha_ = 1.0f;
        static constexpr int kOffsets[8][2] = {{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}};
        static thread_local std::mt19937 rng(std::random_device{}());
        const auto& off = kOffsets[std::uniform_int_distribution<>(0, 7)(rng)];
        const int nr = bestMove->x + off[0];
        const int nc = bestMove->y + off[1];
        if (nr >= 0 && nr < Board::kBoardSize && nc >= 0 && nc < Board::kBoardSize &&
            board_.canPlaceAt(nr, nc)) {
            bestMove = sf::Vector2i{nr, nc};
        }
    }

    // Hierophant restriction: AI move must be in center 9x9
    if (hierophantRemaining_ > 0) {
        if (bestMove->x < 3 || bestMove->x > 11 || bestMove->y < 3 || bestMove->y > 11) {
            // Re-pick a simpler move in the center zone
            std::vector<sf::Vector2i> centerCells;
            for (int r = 3; r <= 11; ++r)
                for (int c = 3; c <= 11; ++c)
                    if (board_.canPlaceAt(r, c)) centerCells.push_back({r, c});
            if (!centerCells.empty()) {
                static thread_local std::mt19937 rng2(std::random_device{}());
                bestMove = centerCells[std::uniform_int_distribution<std::size_t>(0, centerCells.size() - 1)(rng2)];
            } else {
                return;
            }
        }
    }

    if (!board_.placePieceAt(bestMove->x, bestMove->y, currentTurn_)) {
        return;
    }

    playPlaceSound();

    turnTimedOut_ = false;
    turnTimer_.restart();

    winner_ = board_.winnerFromLastMove();
    if (winner_ != Board::Piece::None) {
        gameOver_ = true;
    } else if (!board_.hasEmptyCell()) {
        gameOver_ = true;
    } else {
        currentTurn_ = nextTurn(currentTurn_);
    }

    // Check card event trigger after AI places (total pieces threshold)
    if (!gameOver_ && cardEventState_ == CardEventState::Idle) {
        const int totalPieces = board_.totalPieceCount();
        if (totalPieces >= nextCardThreshold_) {
            triggerCardEvent();
            nextCardThreshold_ += 8;
        }
    }

    // Decrement turn-based card effects
    if (hierophantRemaining_ > 0) --hierophantRemaining_;
    if (hermitRemaining_ > 0) --hermitRemaining_;
    if (temperanceRemaining_ > 0) {
        --temperanceRemaining_;
        if (temperanceRemaining_ == 0) board_.setTemperanceActive(false);
    }
    if (strengthProtectionRemaining_ > 0) {
        --strengthProtectionRemaining_;
        if (strengthProtectionRemaining_ == 0) {
            board_.setStrengthPos(std::nullopt);
            strengthProtectedPos_ = {-1, -1};
        }
    }

    updateHardModeObstacles();
}

void Game::autoPlaceRandom() {
    if (gameOver_ || deathAnimPending_ || loversAnimPending_ || worldAnimPending_ || strengthAnimPending_ || empressAnimPending_ || highPriestessAnimPending_ || sunAnimPending_ || moonAnimPending_ || hermitAnimPending_ || hierophantAnimPending_ || justiceAnimPending_ || hangedManAnimPending_ || devilAnimPending_ || chariotAnimPending_ || magicianAnimPending_ || temperanceAnimPending_ || foolAnimPending_ || towerAnimPending_ || wheelFortuneAnimPending_ || judgementAnimPending_ || (starAnimPending_ && !starInPersistentMode_)) return;

    std::vector<sf::Vector2i> emptyCells;
    for (int row = 0; row < Board::kBoardSize; ++row) {
        for (int col = 0; col < Board::kBoardSize; ++col) {
            if (board_.canPlaceAt(row, col)) {
                emptyCells.push_back({row, col});
            }
        }
    }
    if (emptyCells.empty()) {
        gameOver_ = true;
        return;
    }

    static thread_local std::mt19937 rng(std::random_device{}());
    const auto& cell = emptyCells[std::uniform_int_distribution<std::size_t>(0, emptyCells.size() - 1)(rng)];

    if (!board_.placePieceAt(cell.x, cell.y, currentTurn_)) return;

    turnTimedOut_ = false;
    turnTimer_.restart();

    // Spawn sparks for the auto-placed piece too
    spawnSparks(board_.cellToPixel(cell.x, cell.y), currentTurn_);

    winner_ = board_.winnerFromLastMove();
    if (winner_ != Board::Piece::None) {
        gameOver_ = true;
    } else if (!board_.hasEmptyCell()) {
        gameOver_ = true;
    } else {
        currentTurn_ = nextTurn(currentTurn_);
    }

    updateHardModeObstacles();

    if (networkMode_) {
        if (server_) server_->sendMove(cell.x, cell.y);
        else if (client_) client_->sendMove(cell.x, cell.y);
    }

    if (!obstacleEventPending_ && aiEnabled_ && !gameOver_ && currentTurn_ == Board::Piece::White) {
        aiMovePending_ = true;
        const int roll = std::rand() % 100;
        if (roll < 15)       aiThinkTimeMs_ = 150 + std::rand() % 350;
        else if (roll < 85)  aiThinkTimeMs_ = 700 + std::rand() % 1800;
        else                 aiThinkTimeMs_ = 2000 + std::rand() % 1500;
        aiClock_.restart();
    }
    updateWindowTitle();
}

void Game::updateHardModeObstacles() {
    if (boardMode_ != Board::Mode::Obstacle || !obstacleDynamic_ || gameOver_) {
        return;
    }
    if (networkMode_ && !isNetworkHost_) {
        return;
    }

    // Fool: skip one obstacle event
    if (foolActive_) {
        foolActive_ = false;
        return;
    }

    ++hardModeObstacleMoves_;

    const int pieces = board_.totalPieceCount();
    const int obs = board_.obstacleCount();

    int spawnChance;
    int despawnChance;
    int minObsForDespawn;

    if (pieces < 10) {
        spawnChance = 40;
        despawnChance = 5;
        minObsForDespawn = 6;
    } else if (pieces < 24) {
        spawnChance = 30;
        despawnChance = 10;
        minObsForDespawn = 5;
    } else {
        spawnChance = 20;
        despawnChance = 15;
        minObsForDespawn = 3;
    }

    pendingSpawnAmount_ = 0;
    pendingDespawnAmount_ = 0;

    static thread_local std::mt19937 rng(std::random_device{}());

    if (obs < Board::kMaxObstacles &&
        std::uniform_int_distribution<>(1, 100)(rng) <= spawnChance) {
        pendingSpawnAmount_ = 1;
    }

    if (obs >= minObsForDespawn &&
        std::uniform_int_distribution<>(1, 100)(rng) <= despawnChance) {
        pendingDespawnAmount_ = 1;
    }

    if (pendingSpawnAmount_ > 0 || pendingDespawnAmount_ > 0) {
        obstacleEventPending_ = true;
        obstacleEventClock_.restart();
    }
}

void Game::startNetworkHost() {
    stopNetwork();
    server_ = std::make_unique<NetworkServer>();
    if (!server_->start(networkPort_)) {
        server_.reset();
        return;
    }
    aiEnabled_ = false;
    networkMode_ = true;
    isNetworkHost_ = true;
    networkDisconnected_ = false;
    remainingUndos_ = roomUndoCount_;
    scene_ = Scene::NetworkWait;
    modeSelectClock_.restart();
    waitParticles_.clear();
    updateWindowTitle();
}

void Game::startNetworkJoin() {
    stopNetwork();
    client_ = std::make_unique<NetworkClient>();
    client_->startConnecting(joinIp_, networkPort_);
    aiEnabled_ = false;
    networkMode_ = true;
    isNetworkHost_ = false;
    networkDisconnected_ = false;
    ipInputActive_ = false;
    scene_ = Scene::NetworkWait;
    modeSelectClock_.restart();
    waitParticles_.clear();
    updateWindowTitle();
}

void Game::stopNetwork() {
    if (server_) {
        server_->stop();
        server_.reset();
    }
    if (client_) {
        client_->disconnect();
        client_.reset();
    }
    networkMode_ = false;
    isNetworkHost_ = false;
    networkDisconnected_ = false;
}

void Game::processNetworkEvents() {
    if (!networkMode_) {
        return;
    }

    if (server_) {
        server_->update();
        if (scene_ == Scene::Playing && !server_->isConnected()) {
            networkDisconnected_ = true;
        }
        if (server_->isConnected() && scene_ == Scene::NetworkWait) {
            server_->sendRoomConfig(static_cast<int>(boardMode_), roomUndoCount_, roomTurnTime_, selectedMapIndex_, obstacleDynamic_ ? 1 : 0);
            if (selectedMapIndex_ >= 0 && selectedMapIndex_ < static_cast<int>(mapFiles_.size()) &&
                std::filesystem::exists(mapFiles_[selectedMapIndex_])) {
                static_cast<void>(gameBgTex_.loadFromFile(mapFiles_[selectedMapIndex_]));
                gameBgTex_.setSmooth(false);
            }
            board_.reset(boardMode_);
            scene_ = Scene::Playing;
            restart();
            if (boardMode_ == Board::Mode::Obstacle) {
                server_->sendObstacleSync(board_.obstaclePositions());
            }
        }
        while (server_->hasPendingMove()) {
            auto move = server_->popMove();
            if (move.row == -3 && move.col == -3) {
                gameOver_ = true;
                winner_ = Board::Piece::Black;
                updateWindowTitle();
                continue;
            }
            if (move.row == -2 && move.col == -2) {
                const auto undone = board_.undoLastMove();
                if (undone.has_value()) {
                    currentTurn_ = undone->piece;
                }
                winner_ = Board::Piece::None;
                gameOver_ = false;
                aiMovePending_ = false;
                obstacleEventPending_ = false;
                turnTimedOut_ = false;
                turnTimer_.restart();
                updateWindowTitle();
                continue;
            }
            if (move.row == -1 && move.col == -1) {
                restart();
                if (boardMode_ == Board::Mode::Obstacle) {
                    server_->sendObstacleSync(board_.obstaclePositions());
                }
                continue;
            }
            if (move.row == -5) {
                // Client's card selection — apply to server side
                if (cardEventState_ == CardEventState::Choosing) {
                    const int idx = move.col;
                    if (idx >= 0 && idx < kCardsPerEvent) {
                        chosenCardIdx_ = idx;
                        selectedCardIndex_ = idx;
                        cardEventState_ = CardEventState::Reveal;
                        cardEventClock_.restart();
                        cardRevealProgress_ = 0.0f;
                        cardEventSpeech_.clear();
                        // Server generates RNG seed for deterministic card effects
                        cardEffectSeed_ = static_cast<std::uint32_t>(std::rand());
                        cardEffectSeedSet_ = true;
                        server_->sendCardEffectSeed(cardEffectSeed_);
                    }
                }
                continue;
            }
            if (!gameOver_ && !isMyTurn()) {
                board_.placePieceAt(move.row, move.col, currentTurn_);
                playPlaceSound();
                winner_ = board_.winnerFromLastMove();
                if (winner_ != Board::Piece::None || !board_.hasEmptyCell()) {
                    gameOver_ = true;
                } else {
                    // Check card event trigger before switching turn
                    bool cardTriggered = false;
                    if (cardEventState_ == CardEventState::Idle) {
                        const int totalPieces = board_.totalPieceCount();
                        if (totalPieces >= nextCardThreshold_) {
                            triggerCardEvent();
                            nextCardThreshold_ += 8;
                            cardTriggered = true;
                        }
                    }
                    if (!cardTriggered) {
                        currentTurn_ = nextTurn(currentTurn_);
                    } else {
                        cardDeferredTurn_ = true;
                    }
                    turnTimedOut_ = false;
                    turnTimer_.restart();
                }
                hardModeObstacleMoves_ = 0;
                updateWindowTitle();
            }
        }
    }

    if (client_) {
        client_->update();
        if (scene_ == Scene::Playing && !client_->isConnected()) {
            networkDisconnected_ = true;
        }
        if (client_->isConnected() && client_->hasRoomConfig() && scene_ == Scene::NetworkWait) {
            const auto cfg = client_->roomConfig();
            boardMode_ = static_cast<Board::Mode>(cfg.mode);
            roomUndoCount_ = cfg.undoCount;
            roomTurnTime_ = cfg.turnTime;
            turnTimeLimit_ = cfg.turnTime;
            selectedMapIndex_ = cfg.selectedMapIndex;
            obstacleDynamic_ = cfg.obstacleDynamic != 0;
            if (mapFiles_.empty()) {
                for (const auto* prefix : {"assets/", "../assets/"}) {
                    for (int i = 1; i <= 30; ++i) {
                        std::string path;
                        if (i < 10) {
                            path = std::string(prefix) + "Sprite-000" + std::to_string(i) + ".jpg";
                        } else {
                            path = std::string(prefix) + "Sprite-00" + std::to_string(i) + ".jpg";
                        }
                        if (std::filesystem::exists(path)) {
                            mapFiles_.push_back(path);
                        }
                    }
                    if (!mapFiles_.empty()) break;
                }
            }
            if (selectedMapIndex_ >= 0 && selectedMapIndex_ < static_cast<int>(mapFiles_.size()) &&
                std::filesystem::exists(mapFiles_[selectedMapIndex_])) {
                static_cast<void>(gameBgTex_.loadFromFile(mapFiles_[selectedMapIndex_]));
                gameBgTex_.setSmooth(false);
            }
            board_.reset(boardMode_);
            scene_ = Scene::Playing;
            restart();
        }
        while (client_->hasPendingObstacleSync()) {
            board_.setObstacles(client_->popObstacleSync());
        }
        while (client_->hasPendingMove()) {
            auto move = client_->popMove();
            if (move.row == -3 && move.col == -3) {
                gameOver_ = true;
                winner_ = Board::Piece::White;
                updateWindowTitle();
                continue;
            }
            if (move.row == -2 && move.col == -2) {
                const auto undone = board_.undoLastMove();
                if (undone.has_value()) {
                    currentTurn_ = undone->piece;
                }
                winner_ = Board::Piece::None;
                gameOver_ = false;
                aiMovePending_ = false;
                obstacleEventPending_ = false;
                turnTimedOut_ = false;
                turnTimer_.restart();
                updateWindowTitle();
                continue;
            }
            if (move.row == -1 && move.col == -1) {
                restart();
                continue;
            }
            if (move.row == -4) {
                // Server's card event data — overwrite local cards
                drawnCards_[0] = static_cast<CardType>((move.col >> 16) & 0xFF);
                drawnCards_[1] = static_cast<CardType>((move.col >> 8) & 0xFF);
                drawnCards_[2] = static_cast<CardType>(move.col & 0xFF);
                if (cardEventState_ == CardEventState::Idle) {
                    // Client hasn't entered card event yet — enter now
                    selectedCardIndex_ = -1;
                    chosenCardIdx_ = -1;
                    cardEventState_ = CardEventState::Omen;
                    cardEventClock_.restart();
                    shakeIntensity_ = 0.0f;
                    darkenAlpha_ = 0.0f;
                    messengerAlpha_ = 0.0f;
                    messengerPos_ = {640.0f, 410.0f};
                    cardDealProgress_ = 0.0f;
                    cardFlipProgress_ = 0.0f;
                    cardFloatTime_ = 0.0f;
                    cardRevealProgress_ = 0.0f;
                    cardEventSpeech_.clear();
                    messengerFloatClock_.restart();
                }
                continue;
            }
            if (move.row == -5) {
                // Server's card selection — apply to client side
                if (cardEventState_ == CardEventState::Choosing) {
                    const int idx = move.col;
                    if (idx >= 0 && idx < kCardsPerEvent) {
                        chosenCardIdx_ = idx;
                        selectedCardIndex_ = idx;
                        cardEventState_ = CardEventState::Reveal;
                        cardEventClock_.restart();
                        cardRevealProgress_ = 0.0f;
                        cardEventSpeech_.clear();
                    }
                }
                continue;
            }
            if (move.row == -7) {
                // Server's wheel result
                wheelResultHost_ = move.col != 0;
                wheelResultSet_ = true;
                iAmPicker_ = wheelResultHost_ ? isNetworkHost_ : !isNetworkHost_;
                continue;
            }
            if (move.row == -8) {
                // Server sends synced wheel start angle — auto-transition from MessengerWait
                if (cardEventState_ == CardEventState::MessengerWait) {
                    cardEventState_ = CardEventState::WheelSpin;
                    wheelSpinClock_.restart();
                    wheelAngle_ = 0.0f;
                    wheelStartAngle_ = static_cast<float>(move.col);
                    wheelResultHost_ = false;
                    wheelResultSet_ = false;
                    wheelStopped_ = false;
                    iAmPicker_ = false;
                }
                continue;
            }
            if (move.row == -9) {
                // Server's RNG seed for card effects — ensures deterministic results
                cardEffectSeed_ = static_cast<std::uint32_t>(move.col);
                cardEffectSeedSet_ = true;
                continue;
            }
            if (!gameOver_ && !isMyTurn()) {
                board_.placePieceAt(move.row, move.col, currentTurn_);
                playPlaceSound();
                winner_ = board_.winnerFromLastMove();
                if (winner_ != Board::Piece::None || !board_.hasEmptyCell()) {
                    gameOver_ = true;
                } else {
                    // Check card event threshold (server will send actual cards)
                    bool cardDefer = false;
                    if (cardEventState_ == CardEventState::Idle) {
                        const int totalPieces = board_.totalPieceCount();
                        if (totalPieces >= nextCardThreshold_) {
                            nextCardThreshold_ += 8;
                            cardDefer = true;
                        }
                    }
                    if (!cardDefer) {
                        currentTurn_ = nextTurn(currentTurn_);
                    } else {
                        cardDeferredTurn_ = true;
                    }
                    turnTimedOut_ = false;
                    turnTimer_.restart();
                }
                hardModeObstacleMoves_ = 0;
                updateWindowTitle();
            }
        }
    }
}

bool Game::isMyTurn() const {
    if (!networkMode_) {
        return true;
    }
    if (isNetworkHost_) {
        return currentTurn_ == Board::Piece::Black;
    }
    return currentTurn_ == Board::Piece::White;
}

void Game::handleNetworkLobbyClick(sf::Vector2f mousePosition) {
    if (buttonAnimIndex_ >= 0) return;

    const auto texSize = modeBtnTex_.getSize();
    if (texSize.x == 0) return;
    const float texW = static_cast<float>(texSize.x);
    const float texH = static_cast<float>(texSize.y);
    const float mainScale = 120.0f / texH;
    const float mainW = texW * mainScale;
    const float mainH = texH * mainScale;

    const float colGap = 36.0f;
    const float rowGap = 22.0f;
    const float totalW = mainW * 2.0f + colGap;
    const float leftX = (1280.0f - totalW) * 0.5f;
    const float rightX = leftX + mainW + colGap;
    const float row1Y = 260.0f;
    const float row2Y = row1Y + mainH + rowGap;

    const struct { sf::FloatRect rect; int idx; } btns[] = {
        {sf::FloatRect({leftX, row1Y}, {mainW, mainH}), 400},
        {sf::FloatRect({rightX, row1Y}, {mainW, mainH}), 401},
        {sf::FloatRect({(1280.0f - mainW) * 0.5f, row2Y}, {mainW, mainH}), 402},
    };
    for (const auto& [rect, idx] : btns) {
        if (rect.contains(mousePosition)) {
            buttonAnimIndex_ = idx;
            buttonAnimClock_.restart();
            return;
        }
    }
}

void Game::handleNetworkWaitClick(sf::Vector2f mousePosition) {
    if (buttonAnimIndex_ >= 0) return;

    // IP input box click — toggle active state
    if (!isNetworkHost_) {
        const float ibX = 240.0f;
        const float ibY = 230.0f;
        const float ibW = 800.0f;
        const float ibH = 56.0f;
        if (sf::FloatRect({ibX, ibY}, {ibW, ibH}).contains(mousePosition)) {
            ipInputActive_ = !ipInputActive_;
            return;
        }
        // Click outside input box deactivates it
        if (ipInputActive_) {
            ipInputActive_ = false;
        }
    }

    // Button hit testing (matches drawNetworkWaitScene)
    if (modeBtnTex_.getSize().x > 0) {
        const auto texSize = modeBtnTex_.getSize();
        const float texH = static_cast<float>(texSize.y);
        const float btnScale = 100.0f / texH;
        const float btnW = static_cast<float>(texSize.x) * btnScale;
        const float btnH = texH * btnScale;
        const float bY = 500.0f;

        if (isNetworkHost_) {
            const float bX = (1280.0f - btnW) * 0.5f;
            if (sf::FloatRect({bX, bY}, {btnW, btnH}).contains(mousePosition)) {
                buttonAnimIndex_ = 403;
                buttonAnimClock_.restart();
                return;
            }
        } else {
            const float bGap = 32.0f;
            if (!joinIp_.empty() && (!client_ || (!client_->isConnecting() && !client_->isConnected()))) {
                const float totalW = btnW * 2.0f + bGap;
                const float leftX = (1280.0f - totalW) * 0.5f;
                if (sf::FloatRect({leftX, bY}, {btnW, btnH}).contains(mousePosition)) {
                    buttonAnimIndex_ = 404;
                    buttonAnimClock_.restart();
                    return;
                }
                if (sf::FloatRect({leftX + btnW + bGap, bY}, {btnW, btnH}).contains(mousePosition)) {
                    buttonAnimIndex_ = 403;
                    buttonAnimClock_.restart();
                    return;
                }
            } else {
                const float bX = (1280.0f - btnW) * 0.5f;
                if (sf::FloatRect({bX, bY}, {btnW, btnH}).contains(mousePosition)) {
                    buttonAnimIndex_ = 403;
                    buttonAnimClock_.restart();
                    return;
                }
            }
        }
    }
}

void Game::drawNetworkLobbyScene() {
    // Background image (222.png)
    if (networkBgTex_.getSize().x > 0) {
        sf::Sprite bgSpr(networkBgTex_);
        const auto texSize = networkBgTex_.getSize();
        bgSpr.setScale({1280.0f / static_cast<float>(texSize.x),
                        820.0f / static_cast<float>(texSize.y)});
        window_.draw(bgSpr);
    } else if (menuBgTex_.getSize().x > 0) {
        sf::Sprite bgSpr(menuBgTex_);
        const auto texSize = menuBgTex_.getSize();
        bgSpr.setScale({1280.0f / static_cast<float>(texSize.x),
                        820.0f / static_cast<float>(texSize.y)});
        window_.draw(bgSpr);
    } else {
        sf::RectangleShape bg0;
        bg0.setPosition({0.0f, 0.0f});
        bg0.setSize({1280.0f, 820.0f});
        bg0.setFillColor(sf::Color(22, 21, 30));
        window_.draw(bg0);
    }

    const float lobbyTime = modeSelectClock_.getElapsedTime().asSeconds();

    // Noise blur overlay — drifting + alpha pulse
    if (noiseTex_.getSize().x > 0) {
        const int driftX = static_cast<int>(lobbyTime * 12.0f) % 64;
        const int driftY = static_cast<int>(lobbyTime * 8.0f) % 64;
        const auto noiseAlpha = static_cast<std::uint8_t>(18.0f + std::sin(lobbyTime * 0.7f) * 6.0f);
        sf::Sprite noiseSpr(noiseTex_);
        noiseSpr.setPosition({0.0f, 0.0f});
        noiseSpr.setTextureRect(sf::IntRect({driftX, driftY}, {1280, 820}));
        noiseSpr.setColor(sf::Color(255, 255, 255, noiseAlpha));
        window_.draw(noiseSpr);
    }

    // Subtle dark vignette — breathing alpha
    {
        const auto vigAlpha = static_cast<std::uint8_t>(60.0f + std::sin(lobbyTime * 0.5f) * 15.0f);
        sf::RectangleShape overlay;
        overlay.setPosition({0.0f, 0.0f});
        overlay.setSize({1280.0f, 820.0f});
        overlay.setFillColor(sf::Color(10, 8, 18, vigAlpha));
        window_.draw(overlay);
    }

    // Firefly particles
    {
        constexpr int kFireflyCount = 22;
        for (int i = 0; i < kFireflyCount; ++i) {
            const float phase = static_cast<float>(i) * 2.4f;
            const float id = static_cast<float>(i);
            const float driftSpeed = 0.25f + std::fmod(id * 0.07f, 0.15f);
            const float swayAmp = 40.0f + std::fmod(id * 13.0f, 50.0f);
            const float x = 60.0f + std::fmod(
                id * 143.0f + std::sin(lobbyTime * driftSpeed + phase) * swayAmp
                    + std::cos(lobbyTime * 0.37f + phase * 1.3f) * 25.0f,
                1160.0f);
            const float y = 820.0f - std::fmod(
                lobbyTime * (8.0f + std::fmod(id * 3.0f, 10.0f)) + id * 73.0f,
                900.0f);

            const float pulse1 = std::sin(lobbyTime * 0.9f + phase) * 0.5f + 0.5f;
            const float pulse2 = std::sin(lobbyTime * 1.7f + phase * 2.1f) * 0.5f + 0.5f;
            const float brightness = pulse1 * 0.65f + pulse2 * 0.35f;
            const float coreAlpha = 0.2f + brightness * 0.8f;
            const float glowAlpha = coreAlpha * 0.35f;

            if (coreAlpha < 0.15f) continue;

            const auto fireflyColor = sf::Color(255, 210, 100,
                                                static_cast<std::uint8_t>(coreAlpha * 220.0f));
            const auto glowColor = sf::Color(255, 200, 80,
                                              static_cast<std::uint8_t>(glowAlpha * 160.0f));

            sf::CircleShape glow(7.0f);
            glow.setOrigin({7.0f, 7.0f});
            glow.setPosition({x, y});
            glow.setFillColor(glowColor);
            window_.draw(glow);

            sf::CircleShape core(2.0f);
            core.setOrigin({2.0f, 2.0f});
            core.setPosition({x, y});
            core.setFillColor(fireflyColor);
            window_.draw(core);
        }
    }

    // Animated title with float + 3D dual-layer
    {
        const float titleFloat = std::sin(lobbyTime * 1.6f) * 4.0f;
        const float subFloat = std::sin(lobbyTime * 1.6f + 1.2f) * 2.5f;

        const float titleX = 88.0f;
        const float titleBaseY = 72.0f + titleFloat;
        const float subX = 94.0f;
        const float subBaseY = 144.0f + subFloat;

        drawText("局域网联机对战", {titleX + 4.0f, titleBaseY - 2.0f}, 54,
                 sf::Color(18, 12, 28, 200), sf::Text::Bold);
        drawText("局域网联机对战", {titleX, titleBaseY}, 54,
                 sf::Color(255, 242, 210), sf::Text::Bold);

        drawText("请选择你的角色", {subX + 2.0f, subBaseY - 1.0f}, 22,
                 sf::Color(12, 8, 20, 160), sf::Text::Bold);
        drawText("请选择你的角色", {subX, subBaseY}, 22,
                 sf::Color(220, 205, 175), sf::Text::Bold);
    }

    const sf::Vector2f mousePosition = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));

    // Buttons using 4.png
    if (modeBtnTex_.getSize().x > 0) {
        const auto texSize = modeBtnTex_.getSize();
        const float texW = static_cast<float>(texSize.x);
        const float texH = static_cast<float>(texSize.y);
        const float mainScale = 120.0f / texH;
        const float mainW = texW * mainScale;
        const float mainH = texH * mainScale;

        const float colGap = 36.0f;
        const float rowGap = 22.0f;
        const float totalW = mainW * 2.0f + colGap;
        const float leftX = (1280.0f - totalW) * 0.5f;
        const float rightX = leftX + mainW + colGap;
        const float row1Y = 260.0f;
        const float row2Y = row1Y + mainH + rowGap;

        struct NetBtn {
            sf::Vector2f pos;
            std::string label;
            int animIdx;
        };

        const NetBtn buttons[] = {
            {{leftX, row1Y}, "创建房间", 400},
            {{rightX, row1Y}, "加入房间", 401},
            {{(1280.0f - mainW) * 0.5f, row2Y}, "返回模式选择", 402},
        };

        for (const auto& btn : buttons) {
            const bool hovered = sf::FloatRect(btn.pos, {mainW, mainH}).contains(mousePosition);
            const bool pressed = buttonAnimIndex_ == btn.animIdx;
            const float scale = pressed ? 0.92f : 1.0f;

            const float drawW = mainW * scale;
            const float drawH = mainH * scale;
            const float drawX = btn.pos.x + (mainW - drawW) * 0.5f;
            const float drawY = btn.pos.y + (mainH - drawH) * 0.5f;
            const float sprScale = mainScale * scale;

            sf::Sprite btnSpr(modeBtnTex_);
            btnSpr.setPosition({drawX, drawY});
            btnSpr.setScale({sprScale, sprScale});
            if (hovered) {
                btnSpr.setColor(sf::Color(255, 255, 235));
            }
            window_.draw(btnSpr);

            drawCenteredText(btn.label, sf::FloatRect({drawX, drawY}, {drawW, drawH}),
                             static_cast<unsigned>(26.0f * scale),
                             sf::Color(252, 248, 238, 240), sf::Text::Bold);

            if (hovered && !pressed) {
                sf::RectangleShape glow;
                glow.setPosition({drawX, drawY});
                glow.setSize({drawW, drawH});
                glow.setFillColor(sf::Color(255, 255, 220, 24));
                window_.draw(glow);
            }
        }
    }
}

void Game::drawRoomSetupScene() {
    // Background image (222.png)
    if (networkBgTex_.getSize().x > 0) {
        sf::Sprite bgSpr(networkBgTex_);
        const auto texSize = networkBgTex_.getSize();
        bgSpr.setScale({1280.0f / static_cast<float>(texSize.x),
                        820.0f / static_cast<float>(texSize.y)});
        window_.draw(bgSpr);
    } else if (menuBgTex_.getSize().x > 0) {
        sf::Sprite bgSpr(menuBgTex_);
        const auto texSize = menuBgTex_.getSize();
        bgSpr.setScale({1280.0f / static_cast<float>(texSize.x),
                        820.0f / static_cast<float>(texSize.y)});
        window_.draw(bgSpr);
    } else {
        sf::RectangleShape bg0;
        bg0.setPosition({0.0f, 0.0f});
        bg0.setSize({1280.0f, 820.0f});
        bg0.setFillColor(sf::Color(22, 21, 30));
        window_.draw(bg0);
    }

    const float lobbyTime = modeSelectClock_.getElapsedTime().asSeconds();

    // Noise blur overlay
    if (noiseTex_.getSize().x > 0) {
        const int driftX = static_cast<int>(lobbyTime * 12.0f) % 64;
        const int driftY = static_cast<int>(lobbyTime * 8.0f) % 64;
        const auto noiseAlpha = static_cast<std::uint8_t>(18.0f + std::sin(lobbyTime * 0.7f) * 6.0f);
        sf::Sprite noiseSpr(noiseTex_);
        noiseSpr.setPosition({0.0f, 0.0f});
        noiseSpr.setTextureRect(sf::IntRect({driftX, driftY}, {1280, 820}));
        noiseSpr.setColor(sf::Color(255, 255, 255, noiseAlpha));
        window_.draw(noiseSpr);
    }

    // Subtle dark vignette
    {
        const auto vigAlpha = static_cast<std::uint8_t>(60.0f + std::sin(lobbyTime * 0.5f) * 15.0f);
        sf::RectangleShape overlay;
        overlay.setPosition({0.0f, 0.0f});
        overlay.setSize({1280.0f, 820.0f});
        overlay.setFillColor(sf::Color(10, 8, 18, vigAlpha));
        window_.draw(overlay);
    }

    // Firefly particles
    {
        constexpr int kFireflyCount = 22;
        for (int i = 0; i < kFireflyCount; ++i) {
            const float phase = static_cast<float>(i) * 2.4f;
            const float id = static_cast<float>(i);
            const float driftSpeed = 0.25f + std::fmod(id * 0.07f, 0.15f);
            const float swayAmp = 40.0f + std::fmod(id * 13.0f, 50.0f);
            const float x = 60.0f + std::fmod(
                id * 143.0f + std::sin(lobbyTime * driftSpeed + phase) * swayAmp
                    + std::cos(lobbyTime * 0.37f + phase * 1.3f) * 25.0f,
                1160.0f);
            const float y = 820.0f - std::fmod(
                lobbyTime * (8.0f + std::fmod(id * 3.0f, 10.0f)) + id * 73.0f,
                900.0f);

            const float pulse1 = std::sin(lobbyTime * 0.9f + phase) * 0.5f + 0.5f;
            const float pulse2 = std::sin(lobbyTime * 1.7f + phase * 2.1f) * 0.5f + 0.5f;
            const float brightness = pulse1 * 0.65f + pulse2 * 0.35f;
            const float coreAlpha = 0.2f + brightness * 0.8f;
            const float glowAlpha = coreAlpha * 0.35f;

            if (coreAlpha < 0.15f) continue;

            const auto fireflyColor = sf::Color(255, 210, 100,
                                                static_cast<std::uint8_t>(coreAlpha * 220.0f));
            const auto glowColor = sf::Color(255, 200, 80,
                                              static_cast<std::uint8_t>(glowAlpha * 160.0f));

            sf::CircleShape glow(7.0f);
            glow.setOrigin({7.0f, 7.0f});
            glow.setPosition({x, y});
            glow.setFillColor(glowColor);
            window_.draw(glow);

            sf::CircleShape core(2.0f);
            core.setOrigin({2.0f, 2.0f});
            core.setPosition({x, y});
            core.setFillColor(fireflyColor);
            window_.draw(core);
        }
    }

    // Animated title
    {
        const float titleFloat = std::sin(lobbyTime * 1.6f) * 4.0f;
        const float subFloat = std::sin(lobbyTime * 1.6f + 1.2f) * 2.5f;
        const float titleX = 88.0f;
        const float titleBaseY = 58.0f + titleFloat;
        const float subY = 130.0f + subFloat;

        drawText("房间设置", {titleX + 4.0f, titleBaseY - 2.0f}, 54,
                 sf::Color(18, 12, 28, 200), sf::Text::Bold);
        drawText("房间设置", {titleX, titleBaseY}, 54,
                 sf::Color(255, 242, 210), sf::Text::Bold);

        drawText("自定义对局规则，完成后创建房间等待对手加入", {98.0f + 2.0f, subY - 1.0f}, 20,
                 sf::Color(12, 8, 20, 160), sf::Text::Bold);
        drawText("自定义对局规则，完成后创建房间等待对手加入", {98.0f, subY}, 20,
                 sf::Color(200, 188, 160));
    }

    const sf::Vector2f mousePosition = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));

    // Settings card
    const float cardLeft = 140.0f;
    const float cardTop = 175.0f;
    const float cardWidth = 1000.0f;
    const float cardHeight = 480.0f;

    // Card glass background
    {
        sf::RectangleShape cardBg;
        cardBg.setPosition({cardLeft, cardTop});
        cardBg.setSize({cardWidth, cardHeight});
        cardBg.setFillColor(sf::Color(25, 16, 35, 140));
        cardBg.setOutlineThickness(1.5f);
        cardBg.setOutlineColor(sf::Color(90, 70, 120, 80));
        window_.draw(cardBg);

        // Card inner highlight line at top
        sf::RectangleShape cardHighlight;
        cardHighlight.setPosition({cardLeft + 2.0f, cardTop + 1.0f});
        cardHighlight.setSize({cardWidth - 4.0f, 1.0f});
        cardHighlight.setFillColor(sf::Color(140, 115, 180, 50));
        window_.draw(cardHighlight);
    }

    const float labelX = cardLeft + 36.0f;
    const float optsX = cardLeft + 300.0f;
    const float rowHeight = 86.0f;
    const float firstRowY = cardTop + 20.0f;

    auto drawSettingRow = [&](int rowIdx, std::string_view label) {
        const float rowY = firstRowY + rowIdx * rowHeight;
        // Row separator
        if (rowIdx > 0) {
            sf::RectangleShape sep;
            sep.setPosition({labelX, rowY - 10.0f});
            sep.setSize({cardWidth - 72.0f, 1.0f});
            sep.setFillColor(sf::Color(80, 60, 100, 60));
            window_.draw(sep);
        }
        // Label
        drawText(label, {labelX, rowY + 10.0f}, 24,
                 sf::Color(225, 215, 195), sf::Text::Bold);
        return rowY;
    };

    auto drawOptionChip = [&](sf::Vector2f pos, sf::Vector2f size, std::string_view text,
                               bool selected, bool hovered, unsigned fontSize) {
        // Drop shadow
        {
            sf::RectangleShape shadow;
            shadow.setPosition({pos.x + 2.5f, pos.y + 2.5f});
            shadow.setSize(size);
            shadow.setFillColor(sf::Color(15, 12, 22, 100));
            window_.draw(shadow);
        }

        // Main box — frosted glass matching stepper
        const auto fill = selected
            ? sf::Color(58, 52, 78, 220)
            : sf::Color(32, 28, 48, 200);
        const auto borderColor = selected
            ? sf::Color(160, 148, 200, 170)
            : sf::Color(100, 90, 135, 125);
        const auto borderThick = selected ? 2.0f : 1.2f;

        sf::RectangleShape chip;
        chip.setPosition(pos);
        chip.setSize(size);
        chip.setFillColor(fill);
        chip.setOutlineThickness(borderThick);
        chip.setOutlineColor(borderColor);
        window_.draw(chip);

        // Top highlight
        {
            sf::RectangleShape bevel;
            bevel.setPosition({pos.x + 2.5f, pos.y + 1.5f});
            bevel.setSize({size.x - 5.0f, 1.5f});
            const auto bevelAlpha = static_cast<std::uint8_t>(selected ? 45.0f : 30.0f);
            bevel.setFillColor(sf::Color(255, 255, 255, bevelAlpha));
            window_.draw(bevel);
        }

        // Selected: subtle inner glow
        if (selected) {
            sf::RectangleShape innerGlow;
            innerGlow.setPosition({pos.x + 1.0f, pos.y + 1.0f});
            innerGlow.setSize({size.x - 2.0f, size.y - 2.0f});
            innerGlow.setFillColor(sf::Color(180, 170, 220, 14));
            window_.draw(innerGlow);
        }

        // Hover highlight on unselected
        if (hovered && !selected) {
            sf::RectangleShape hoverGlow;
            hoverGlow.setPosition(pos);
            hoverGlow.setSize(size);
            hoverGlow.setFillColor(sf::Color(255, 255, 245, 10));
            window_.draw(hoverGlow);
        }

        drawCenteredText(text, sf::FloatRect(pos, size), fontSize,
                         selected ? sf::Color(245, 238, 218) : sf::Color(185, 178, 162),
                         selected ? sf::Text::Bold : sf::Text::Regular);
    };

    const float chipW = 148.0f;
    const float chipH = 46.0f;
    const float chipGap = 16.0f;

    // Stepper control: ◀ [value] ▶
    constexpr float kStepperArrowW = 36.0f;
    constexpr float kStepperArrowH = 44.0f;
    constexpr float kStepperBoxW = 120.0f;
    constexpr float kStepperBoxH = 44.0f;
    constexpr float kStepperGap = 10.0f;
    constexpr float kStepperAnimDuration = 0.18f;
    const float stepperCenterX = optsX + 120.0f;
    const float stepperLeftX = stepperCenterX - kStepperArrowW - kStepperGap - kStepperBoxW * 0.5f;
    const float stepperBoxX = stepperCenterX - kStepperBoxW * 0.5f;
    const float stepperRightX = stepperCenterX + kStepperBoxW * 0.5f + kStepperGap;

    auto drawStepper = [&](int rowIdx, std::string_view labelText,
                            int currentVal, const std::vector<int>& values,
                            std::string_view unit) {
        const float rowY = drawSettingRow(rowIdx, labelText);
        const float sY = rowY + 5.0f;
        const bool isAnimating = stepperAnimRow_ == rowIdx && stepperAnimDir_ != 0;

        int oldVal = currentVal;
        int newVal = currentVal;
        if (isAnimating) {
            oldVal = currentVal;
            newVal = stepperAnimTarget_;
        }

        // Draw static frosted box
        {
            // Drop shadow
            sf::RectangleShape shadow;
            shadow.setPosition({stepperBoxX + 3.0f, sY + 3.0f});
            shadow.setSize({kStepperBoxW, kStepperBoxH});
            shadow.setFillColor(sf::Color(15, 12, 22, 120));
            window_.draw(shadow);

            // Main box
            sf::RectangleShape box;
            box.setPosition({stepperBoxX, sY});
            box.setSize({kStepperBoxW, kStepperBoxH});
            box.setFillColor(sf::Color(32, 28, 48, 210));
            box.setOutlineThickness(1.5f);
            box.setOutlineColor(sf::Color(110, 98, 148, 140));
            window_.draw(box);

            // Top highlight
            sf::RectangleShape bevel;
            bevel.setPosition({stepperBoxX + 3.0f, sY + 1.5f});
            bevel.setSize({kStepperBoxW - 6.0f, 1.5f});
            bevel.setFillColor(sf::Color(255, 255, 255, 35));
            window_.draw(bevel);

            // Bottom shadow edge
            sf::RectangleShape botEdge;
            botEdge.setPosition({stepperBoxX + 3.0f, sY + kStepperBoxH - 2.0f});
            botEdge.setSize({kStepperBoxW - 6.0f, 1.0f});
            botEdge.setFillColor(sf::Color(10, 8, 18, 50));
            window_.draw(botEdge);
        }

        // Draw text with slide animation
        auto drawValueText = [&](int val, float alpha, float slideX) {
            if (alpha <= 0.01f) return;
            const auto valStr = val == 0 ? std::string("禁止") : std::to_string(val) + std::string(unit);
            const auto textAlpha = static_cast<std::uint8_t>(255.0f * alpha);
            drawCenteredText(valStr,
                sf::FloatRect({stepperBoxX + slideX, sY}, {kStepperBoxW, kStepperBoxH}),
                22, sf::Color(245, 238, 218, textAlpha), sf::Text::Bold);
        };

        if (isAnimating) {
            const float elapsed = stepperAnimClock_.getElapsedTime().asSeconds();
            const float t = std::min(1.0f, elapsed / kStepperAnimDuration);
            const float slideDist = 36.0f;
            const float dir = static_cast<float>(stepperAnimDir_);

            const float oldSlideX = -dir * t * slideDist;
            const float oldAlpha = 1.0f - t;
            const float newSlideX = dir * (1.0f - t) * slideDist;
            const float newAlpha = t;

            drawValueText(oldVal, oldAlpha, oldSlideX);
            drawValueText(newVal, newAlpha, newSlideX);
        } else {
            drawValueText(currentVal, 1.0f, 0.0f);
        }

        // Draw arrows
        auto drawArrow = [&](float ax, float aY, bool left, bool hovered, bool enabled) {
            const float cx = ax + kStepperArrowW * 0.5f;
            const float cy = aY + kStepperArrowH * 0.5f;
            const float hw = 7.0f;
            const float hh = 11.0f;

            // Hover glow circle
            if (hovered && enabled) {
                sf::CircleShape glow(17.0f);
                glow.setOrigin({17.0f, 17.0f});
                glow.setPosition({cx, cy});
                glow.setFillColor(sf::Color(255, 245, 220, 18));
                window_.draw(glow);
            }

            sf::Color arrowColor;
            if (!enabled) arrowColor = sf::Color(70, 65, 75, 90);
            else if (hovered) arrowColor = sf::Color(240, 232, 218);
            else arrowColor = sf::Color(175, 168, 155);

            sf::ConvexShape tri(3);
            if (left) {
                tri.setPoint(0, {cx + hw * 0.6f, cy - hh});
                tri.setPoint(1, {cx + hw * 0.6f, cy + hh});
                tri.setPoint(2, {cx - hw * 0.8f, cy});
            } else {
                tri.setPoint(0, {cx - hw * 0.6f, cy - hh});
                tri.setPoint(1, {cx - hw * 0.6f, cy + hh});
                tri.setPoint(2, {cx + hw * 0.8f, cy});
            }
            tri.setFillColor(arrowColor);
            window_.draw(tri);
        };

        const bool leftHover = sf::FloatRect({stepperLeftX, sY}, {kStepperArrowW, kStepperArrowH}).contains(mousePosition);
        const bool rightHover = sf::FloatRect({stepperRightX, sY}, {kStepperArrowW, kStepperArrowH}).contains(mousePosition);

        const auto it3 = std::find(values.begin(), values.end(), currentVal);
        const int curIdx = static_cast<int>(std::distance(values.begin(), it3));
        const bool canDec = curIdx > 0;
        const bool canInc = curIdx < static_cast<int>(values.size()) - 1;

        drawArrow(stepperLeftX, sY, true, leftHover, canDec);
        drawArrow(stepperRightX, sY, false, rightHover, canInc);
    };

    // Row 0: 执子选择
    {
        const float rowY = drawSettingRow(0, "执子选择");
        const struct { sf::Vector2f pos; std::string text; Board::Piece val; } opts[] = {
            {{optsX, rowY + 4.0f}, "执黑先手", Board::Piece::Black},
            {{optsX + chipW + chipGap, rowY + 4.0f}, "执白后手", Board::Piece::White},
        };
        for (const auto& opt : opts) {
            const bool sel = roomHostPiece_ == opt.val;
            const bool hover = sf::FloatRect(opt.pos, {chipW, chipH}).contains(mousePosition);
            drawOptionChip(opt.pos, {chipW, chipH}, opt.text, sel, hover, 20);
        }
    }

    // Row 1: 游戏模式
    {
        const float rowY = drawSettingRow(1, "游戏模式");
        const struct { sf::Vector2f pos; std::string text; Board::Mode val; } opts[] = {
            {{optsX, rowY + 4.0f}, "经典模式", Board::Mode::Classic},
            {{optsX + chipW + chipGap, rowY + 4.0f}, "幻格模式", Board::Mode::Obstacle},
        };
        for (const auto& opt : opts) {
            const bool sel = boardMode_ == opt.val;
            const bool hover = sf::FloatRect(opt.pos, {chipW, chipH}).contains(mousePosition);
            drawOptionChip(opt.pos, {chipW, chipH}, opt.text, sel, hover, 20);
        }

        // 幻格选择 — appears to the right when Obstacle mode is selected
        if (boardMode_ == Board::Mode::Obstacle) {
            const float obsOptsX = optsX + (chipW + chipGap) * 2.0f + 40.0f;
            const float obsChipW = 100.0f;
            const float obsChipH = 40.0f;
            const float obsGap = 12.0f;

            drawText("幻格选择", {obsOptsX, rowY + 16.0f}, 20, sf::Color(190, 175, 140));

            const struct { sf::Vector2f pos; std::string text; bool val; } obsOpts[] = {
                {{obsOptsX + 110.0f, rowY + 6.0f}, "简单", false},
                {{obsOptsX + 110.0f + obsChipW + obsGap, rowY + 6.0f}, "困难", true},
            };
            for (const auto& oo : obsOpts) {
                const bool sel = obstacleDynamic_ == oo.val;
                const bool hover = sf::FloatRect(oo.pos, {obsChipW, obsChipH}).contains(mousePosition);
                drawOptionChip(oo.pos, {obsChipW, obsChipH}, oo.text, sel, hover, 18);
            }
        }
    }

    // Row 2: 悔棋次数 — stepper
    drawStepper(2, "悔棋次数", roomUndoCount_, {0, 1, 2, 3, 4, 5}, " 次");

    // Row 3: 思考时间 — stepper
    drawStepper(3, "思考时间", roomTurnTime_, {5, 6, 7, 8, 9, 10}, " 秒");

    // Row 4: 地图选择
    {
        const float rowY = drawSettingRow(4, "地图选择");
        const float mapBtnW = 140.0f;
        const float mapBtnH = 44.0f;
        const float mapBtnX = cardLeft + cardWidth - mapBtnW - 40.0f;

        std::string statusText;
        if (selectedMapIndex_ >= 0 && selectedMapIndex_ < static_cast<int>(mapFiles_.size())) {
            const auto& path = mapFiles_[selectedMapIndex_];
            auto slash = path.find_last_of("/\\");
            auto name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
            statusText = name;
        } else {
            statusText = "未选择";
        }
        drawText(statusText, {optsX, rowY + 12.0f}, 20,
                 selectedMapIndex_ >= 0 ? sf::Color(200, 188, 160) : sf::Color(160, 80, 70));

        const sf::Vector2f mapBtnPos = {mapBtnX, rowY + 4.0f};
        const bool mapHovered = sf::FloatRect(mapBtnPos, {mapBtnW, mapBtnH}).contains(mousePosition);
        const bool mapActive = selectedMapIndex_ >= 0;
        drawOptionChip(mapBtnPos, {mapBtnW, mapBtnH}, "选择地图", mapActive, mapHovered, 18);
    }

    // Bottom action buttons using 4.png
    if (modeBtnTex_.getSize().x > 0) {
        const auto texSize = modeBtnTex_.getSize();
        const float texW = static_cast<float>(texSize.x);
        const float texH = static_cast<float>(texSize.y);
        const float btnScale = 100.0f / texH;
        const float btnW = texW * btnScale;
        const float btnH = texH * btnScale;
        const float btnGap = 32.0f;
        const float totalBtnW = btnW * 2.0f + btnGap;
        const float btnY = cardTop + cardHeight + 32.0f;

        struct ActionBtn {
            sf::Vector2f pos;
            std::string label;
            int animIdx;
        };
        const ActionBtn actionBtns[] = {
            {{(1280.0f - totalBtnW) * 0.5f, btnY}, "创建房间", 420},
            {{(1280.0f - totalBtnW) * 0.5f + btnW + btnGap, btnY}, "返回", 421},
        };

        for (const auto& ab : actionBtns) {
            const bool isCreateBtn = ab.animIdx == 420;
            const bool disabled = isCreateBtn && selectedMapIndex_ < 0;
            const bool hovered = !disabled && sf::FloatRect(ab.pos, {btnW, btnH}).contains(mousePosition);
            const bool pressed = buttonAnimIndex_ == ab.animIdx;
            const float scale = pressed ? 0.92f : 1.0f;

            const float dW = btnW * scale;
            const float dH = btnH * scale;
            const float dX = ab.pos.x + (btnW - dW) * 0.5f;
            const float dY = ab.pos.y + (btnH - dH) * 0.5f;

            sf::Sprite btnSpr(modeBtnTex_);
            btnSpr.setPosition({dX, dY});
            btnSpr.setScale({btnScale * scale, btnScale * scale});
            if (disabled) btnSpr.setColor(sf::Color(120, 120, 120));
            else if (hovered) btnSpr.setColor(sf::Color(255, 255, 235));
            window_.draw(btnSpr);

            drawCenteredText(ab.label, sf::FloatRect({dX, dY}, {dW, dH}),
                             static_cast<unsigned>(26.0f * scale),
                             disabled ? sf::Color(160, 160, 160, 180) : sf::Color(252, 248, 238, 240),
                             sf::Text::Bold);

            if (hovered && !pressed && !disabled) {
                sf::RectangleShape glow;
                glow.setPosition({dX, dY});
                glow.setSize({dW, dH});
                glow.setFillColor(sf::Color(255, 255, 220, 24));
                window_.draw(glow);
            }
        }
    }

    // Map selection modal overlay
    if (mapSelectOpen_ && !mapFiles_.empty()) {
        // Dark mask
        sf::RectangleShape mask;
        mask.setPosition({0.0f, 0.0f});
        mask.setSize({1280.0f, 820.0f});
        mask.setFillColor(sf::Color(8, 5, 16, 210));
        window_.draw(mask);

        constexpr float kPreviewMaxW = 700.0f;
        constexpr float kPreviewMaxH = 480.0f;
        constexpr float kArrowW = 56.0f;
        constexpr float kArrowH = 80.0f;

        // Load and draw current preview
        if (mapPreviewTex_.getSize().x > 0) {
            const auto texSize = mapPreviewTex_.getSize();
            float scale = std::min(kPreviewMaxW / static_cast<float>(texSize.x),
                                   kPreviewMaxH / static_cast<float>(texSize.y));
            const float pw = static_cast<float>(texSize.x) * scale;
            const float ph = static_cast<float>(texSize.y) * scale;
            const float px = (1280.0f - pw) * 0.5f;
            const float py = (820.0f - ph) * 0.5f;

            sf::RectangleShape previewBg;
            previewBg.setPosition({px - 6.0f, py - 6.0f});
            previewBg.setSize({pw + 12.0f, ph + 12.0f});
            previewBg.setFillColor(sf::Color(25, 20, 40, 200));
            previewBg.setOutlineThickness(2.0f);
            previewBg.setOutlineColor(sf::Color(120, 108, 160, 140));
            window_.draw(previewBg);

            sf::Sprite previewSpr(mapPreviewTex_);
            previewSpr.setPosition({px, py});
            previewSpr.setScale({scale, scale});
            window_.draw(previewSpr);

            // Hover highlight on image
            if (sf::FloatRect({px, py}, {pw, ph}).contains(mousePosition)) {
                sf::RectangleShape imgGlow;
                imgGlow.setPosition({px, py});
                imgGlow.setSize({pw, ph});
                imgGlow.setFillColor(sf::Color(255, 255, 240, 18));
                window_.draw(imgGlow);
            }

            // Left arrow
            const float arrowLY = py + ph * 0.5f - kArrowH * 0.5f;
            const float arrowLX = px - kArrowW - 14.0f;
            const bool leftHov = sf::FloatRect({arrowLX, arrowLY}, {kArrowW, kArrowH}).contains(mousePosition);
            {
                sf::Color ac = leftHov ? sf::Color(240, 232, 218) : sf::Color(175, 168, 155);
                if (leftHov) {
                    sf::CircleShape glowC(22.0f);
                    glowC.setOrigin({22.0f, 22.0f});
                    glowC.setPosition({arrowLX + kArrowW * 0.5f, arrowLY + kArrowH * 0.5f});
                    glowC.setFillColor(sf::Color(255, 245, 220, 18));
                    window_.draw(glowC);
                }
                sf::ConvexShape tri(3);
                tri.setPoint(0, {arrowLX + kArrowW * 0.55f, arrowLY + kArrowH * 0.35f});
                tri.setPoint(1, {arrowLX + kArrowW * 0.55f, arrowLY + kArrowH * 0.65f});
                tri.setPoint(2, {arrowLX + kArrowW * 0.25f, arrowLY + kArrowH * 0.5f});
                tri.setFillColor(ac);
                window_.draw(tri);
            }

            // Right arrow
            const float arrowRY = arrowLY;
            const float arrowRX = px + pw + 14.0f;
            const bool rightHov = sf::FloatRect({arrowRX, arrowRY}, {kArrowW, kArrowH}).contains(mousePosition);
            {
                sf::Color ac = rightHov ? sf::Color(240, 232, 218) : sf::Color(175, 168, 155);
                if (rightHov) {
                    sf::CircleShape glowC(22.0f);
                    glowC.setOrigin({22.0f, 22.0f});
                    glowC.setPosition({arrowRX + kArrowW * 0.5f, arrowRY + kArrowH * 0.5f});
                    glowC.setFillColor(sf::Color(255, 245, 220, 18));
                    window_.draw(glowC);
                }
                sf::ConvexShape tri(3);
                tri.setPoint(0, {arrowRX + kArrowW * 0.45f, arrowRY + kArrowH * 0.35f});
                tri.setPoint(1, {arrowRX + kArrowW * 0.45f, arrowRY + kArrowH * 0.65f});
                tri.setPoint(2, {arrowRX + kArrowW * 0.75f, arrowRY + kArrowH * 0.5f});
                tri.setFillColor(ac);
                window_.draw(tri);
            }

            // Hint text
            drawCenteredText("点击图片确认选择 · 点击外部取消",
                sf::FloatRect({0.0f, py + ph + 20.0f}, {1280.0f, 36.0f}),
                18, sf::Color(180, 170, 155));
        }
    }
}

void Game::handleRoomSetupClick(sf::Vector2f mousePosition) {
    if (buttonAnimIndex_ >= 0 || stepperAnimRow_ >= 0) return;

    // Map selection modal is open — handle modal clicks
    if (mapSelectOpen_) {
        constexpr float kPreviewMaxW = 700.0f;
        constexpr float kPreviewMaxH = 480.0f;
        constexpr float kArrowW = 56.0f;
        constexpr float kArrowH = 80.0f;

        if (mapPreviewTex_.getSize().x > 0) {
            const auto texSize = mapPreviewTex_.getSize();
            float scale = std::min(kPreviewMaxW / static_cast<float>(texSize.x),
                                   kPreviewMaxH / static_cast<float>(texSize.y));
            const float pw = static_cast<float>(texSize.x) * scale;
            const float ph = static_cast<float>(texSize.y) * scale;
            const float px = (1280.0f - pw) * 0.5f;
            const float py = (820.0f - ph) * 0.5f;

            // Click on image → confirm selection
            if (sf::FloatRect({px, py}, {pw, ph}).contains(mousePosition)) {
                selectedMapIndex_ = mapSelectCurrentIdx_;
                mapSelectOpen_ = false;
                return;
            }

            // Left arrow
            const float arrowLY = py + ph * 0.5f - kArrowH * 0.5f;
            if (sf::FloatRect({px - kArrowW - 14.0f, arrowLY}, {kArrowW, kArrowH}).contains(mousePosition)) {
                mapSelectCurrentIdx_ = (mapSelectCurrentIdx_ - 1 + static_cast<int>(mapFiles_.size())) % static_cast<int>(mapFiles_.size());
                if (!mapFiles_.empty()) {
                    static_cast<void>(mapPreviewTex_.loadFromFile(mapFiles_[mapSelectCurrentIdx_]));
                    mapPreviewTex_.setSmooth(true);
                }
                return;
            }

            // Right arrow
            const float arrowRY = py + ph * 0.5f - kArrowH * 0.5f;
            if (sf::FloatRect({px + pw + 14.0f, arrowRY}, {kArrowW, kArrowH}).contains(mousePosition)) {
                mapSelectCurrentIdx_ = (mapSelectCurrentIdx_ + 1) % static_cast<int>(mapFiles_.size());
                if (!mapFiles_.empty()) {
                    static_cast<void>(mapPreviewTex_.loadFromFile(mapFiles_[mapSelectCurrentIdx_]));
                    mapPreviewTex_.setSmooth(true);
                }
                return;
            }
        }

        // Click outside image → cancel
        mapSelectOpen_ = false;
        return;
    }

    const float cardLeft = 140.0f;
    const float cardTop = 175.0f;
    const float cardWidth = 1000.0f;
    const float cardHeight = 480.0f;
    const float optsX = cardLeft + 300.0f;
    const float rowHeight = 86.0f;
    const float firstRowY = cardTop + 20.0f;
    const float chipW = 148.0f;
    const float chipH = 46.0f;
    const float chipGap = 16.0f;

    // Row 0: 执子选择
    {
        const float rowY = firstRowY;
        const struct { sf::FloatRect rect; Board::Piece val; } opts[] = {
            {sf::FloatRect({optsX, rowY + 4.0f}, {chipW, chipH}), Board::Piece::Black},
            {sf::FloatRect({optsX + chipW + chipGap, rowY + 4.0f}, {chipW, chipH}), Board::Piece::White},
        };
        for (const auto& [rect, val] : opts) {
            if (rect.contains(mousePosition)) { roomHostPiece_ = val; return; }
        }
    }

    // Row 1: 游戏模式
    {
        const float rowY = firstRowY + rowHeight;
        const struct { sf::FloatRect rect; Board::Mode val; } opts[] = {
            {sf::FloatRect({optsX, rowY + 4.0f}, {chipW, chipH}), Board::Mode::Classic},
            {sf::FloatRect({optsX + chipW + chipGap, rowY + 4.0f}, {chipW, chipH}), Board::Mode::Obstacle},
        };
        for (const auto& [rect, val] : opts) {
            if (rect.contains(mousePosition)) { boardMode_ = val; return; }
        }

        // 幻格选择 — only visible when Obstacle mode is selected
        if (boardMode_ == Board::Mode::Obstacle) {
            const float obsOptsX = optsX + (chipW + chipGap) * 2.0f + 40.0f;
            const float obsChipW = 100.0f;
            const float obsChipH = 40.0f;
            const float obsGap = 12.0f;
            const struct { sf::FloatRect rect; bool val; } obsOpts[] = {
                {sf::FloatRect({obsOptsX + 110.0f, rowY + 6.0f}, {obsChipW, obsChipH}), false},
                {sf::FloatRect({obsOptsX + 110.0f + obsChipW + obsGap, rowY + 6.0f}, {obsChipW, obsChipH}), true},
            };
            for (const auto& [rect, val] : obsOpts) {
                if (rect.contains(mousePosition)) { obstacleDynamic_ = val; return; }
            }
        }
    }

    // Stepper arrow constants (matching drawStepper)
    const float stArrowW = 36.0f;
    const float stArrowH = 44.0f;
    const float stBoxW = 120.0f;
    const float stGap = 10.0f;
    const float stCenterX = optsX + 120.0f;
    const float stLeftX = stCenterX - stArrowW - stGap - stBoxW * 0.5f;
    const float stRightX = stCenterX + stBoxW * 0.5f + stGap;

    auto handleStepperClick = [&](int rowIdx, int value, const std::vector<int>& vals) {
        if (stepperAnimRow_ >= 0) return;
        const float rowY = firstRowY + rowIdx * rowHeight + 6.0f;
        if (sf::FloatRect({stLeftX, rowY}, {stArrowW, stArrowH}).contains(mousePosition)) {
            auto it = std::find(vals.begin(), vals.end(), value);
            if (it != vals.begin()) {
                stepperAnimRow_ = rowIdx;
                stepperAnimDir_ = -1;
                stepperAnimTarget_ = vals[static_cast<int>(std::distance(vals.begin(), it)) - 1];
                stepperAnimClock_.restart();
            }
            return;
        }
        if (sf::FloatRect({stRightX, rowY}, {stArrowW, stArrowH}).contains(mousePosition)) {
            auto it = std::find(vals.begin(), vals.end(), value);
            if (it != vals.end() - 1) {
                stepperAnimRow_ = rowIdx;
                stepperAnimDir_ = 1;
                stepperAnimTarget_ = vals[static_cast<int>(std::distance(vals.begin(), it)) + 1];
                stepperAnimClock_.restart();
            }
            return;
        }
    };

    handleStepperClick(2, roomUndoCount_, {0, 1, 2, 3, 4, 5});
    handleStepperClick(3, roomTurnTime_, {5, 6, 7, 8, 9, 10});

    // Row 4: 地图选择 button
    {
        const float rowY = firstRowY + 4.0f * rowHeight;
        const float mapBtnW = 140.0f;
        const float mapBtnH = 44.0f;
        const float mapBtnX = cardLeft + cardWidth - mapBtnW - 40.0f;
        if (sf::FloatRect({mapBtnX, rowY + 4.0f}, {mapBtnW, mapBtnH}).contains(mousePosition)) {
            if (mapFiles_.empty()) {
                // Lazy-load map file list
                for (const auto* prefix : {"assets/", "../assets/"}) {
                    for (int i = 1; i <= 30; ++i) {
                        std::string path;
                        if (i < 10) {
                            path = std::string(prefix) + "Sprite-000" + std::to_string(i) + ".jpg";
                        } else {
                            path = std::string(prefix) + "Sprite-00" + std::to_string(i) + ".jpg";
                        }
                        if (std::filesystem::exists(path)) {
                            mapFiles_.push_back(path);
                        }
                    }
                    if (!mapFiles_.empty()) break;
                }
            }
            if (!mapFiles_.empty()) {
                mapSelectCurrentIdx_ = selectedMapIndex_ >= 0 ? selectedMapIndex_ : 0;
                static_cast<void>(mapPreviewTex_.loadFromFile(mapFiles_[mapSelectCurrentIdx_]));
                mapPreviewTex_.setSmooth(true);
                mapSelectOpen_ = true;
                return;
            }
        }
    }

    // Action buttons (using 4.png)
    if (modeBtnTex_.getSize().x > 0) {
        const auto texSize = modeBtnTex_.getSize();
        const float texH = static_cast<float>(texSize.y);
        const float btnScale = 100.0f / texH;
        const float btnW = static_cast<float>(texSize.x) * btnScale;
        const float btnH = texH * btnScale;
        const float btnGap = 32.0f;
        const float totalBtnW = btnW * 2.0f + btnGap;
        const float btnY = cardTop + cardHeight + 32.0f;

        const struct { sf::FloatRect rect; int idx; } btns[] = {
            {sf::FloatRect({(1280.0f - totalBtnW) * 0.5f, btnY}, {btnW, btnH}), 420},
            {sf::FloatRect({(1280.0f - totalBtnW) * 0.5f + btnW + btnGap, btnY}, {btnW, btnH}), 421},
        };
        for (const auto& [rect, idx] : btns) {
            if (rect.contains(mousePosition)) {
                if (idx == 420 && selectedMapIndex_ < 0) return;
                buttonAnimIndex_ = idx;
                buttonAnimClock_.restart();
                return;
            }
        }
    }
}

void Game::drawNetworkWaitScene() {
    const float elapsed = modeSelectClock_.getElapsedTime().asSeconds();

    // Background 222.png with breathing zoom (smooth 1.0 ↔ 1.06, never below screen)
    const float bgZoom = 1.0f + (std::sin(elapsed * 0.7f) * 0.5f + 0.5f) * 0.06f;
    if (networkBgTex_.getSize().x > 0) {
        sf::Sprite bgSpr(networkBgTex_);
        const auto bgSize = networkBgTex_.getSize();
        const float bw = static_cast<float>(bgSize.x);
        const float bh = static_cast<float>(bgSize.y);
        bgSpr.setOrigin({bw * 0.5f, bh * 0.5f});
        bgSpr.setPosition({640.0f, 410.0f});
        bgSpr.setScale({(1280.0f / bw) * bgZoom, (820.0f / bh) * bgZoom});
        window_.draw(bgSpr);
    } else {
        sf::RectangleShape bg1;
        bg1.setPosition({0.0f, 0.0f});
        bg1.setSize({1280.0f, 820.0f});
        bg1.setFillColor(sf::Color(22, 21, 30));
        window_.draw(bg1);
    }

    // Darken overlay for readability
    sf::RectangleShape dim;
    dim.setPosition({0.0f, 0.0f});
    dim.setSize({1280.0f, 820.0f});
    dim.setFillColor(sf::Color(8, 5, 16, 90));
    window_.draw(dim);

    // Particles — gentle upward drift
    const float pElapsed = waitParticleClock_.getElapsedTime().asSeconds();
    if (waitParticleClock_.getElapsedTime().asMilliseconds() >= 50) {
        waitParticleClock_.restart();
        // Spawn new particles
        static thread_local std::mt19937 rng(std::random_device{}());
        while (waitParticles_.size() < 28) {
            TitleParticle p;
            p.pos = {std::uniform_real_distribution<float>(0.0f, 1280.0f)(rng),
                     std::uniform_real_distribution<float>(820.0f, 860.0f)(rng)};
            p.vel = {std::uniform_real_distribution<float>(-8.0f, 8.0f)(rng),
                     std::uniform_real_distribution<float>(-35.0f, -18.0f)(rng)};
            p.life = 0.0f;
            p.maxLife = std::uniform_real_distribution<float>(2.5f, 5.0f)(rng);
            p.color = sf::Color(180, 170, 200, 120);
            waitParticles_.push_back(p);
        }
    }
    for (auto& p : waitParticles_) {
        const float dt = std::min(pElapsed, 0.1f);
        p.life += dt;
        p.pos += p.vel * dt;
        p.vel.x += std::sin(p.life * 1.3f + p.pos.x * 0.01f) * dt * 3.0f;
    }
    waitParticles_.erase(std::remove_if(waitParticles_.begin(), waitParticles_.end(),
        [](const TitleParticle& p) { return p.life >= p.maxLife || p.pos.y < -20.0f; }), waitParticles_.end());
    for (const auto& p : waitParticles_) {
        const float alpha = std::min(1.0f, (1.0f - p.life / p.maxLife) * 1.5f);
        sf::CircleShape dot(1.5f, 6);
        dot.setPosition(p.pos);
        dot.setFillColor(sf::Color(p.color.r, p.color.g, p.color.b,
            static_cast<std::uint8_t>(p.color.a * alpha)));
        window_.draw(dot);
    }

    // Shared animation values for text breathing
    const float textFloat = std::sin(elapsed * 1.2f) * 5.0f;
    const auto textAlpha = static_cast<std::uint8_t>(195.0f + std::sin(elapsed * 1.5f + 1.0f) * 35.0f);
    const auto textAlphaStrong = static_cast<std::uint8_t>(220.0f + std::sin(elapsed * 1.5f + 1.0f) * 30.0f);

    // Dots animation: ".", "..", "...", cycle
    const int dotPhase = (static_cast<int>(elapsed * 2.5f) % 4);
    const std::string dots = dotPhase == 0 ? "" : (dotPhase == 1 ? "." : (dotPhase == 2 ? ".." : "..."));

    if (isNetworkHost_) {
        drawText("等待对手连接" + dots, {88.0f, 100.0f + textFloat}, 46,
                 sf::Color(247, 241, 229, textAlphaStrong), sf::Text::Bold);
        drawText("请将以下地址告诉你的对手：", {94.0f, 180.0f + textFloat * 0.6f}, 22,
                 sf::Color(200, 190, 170, textAlpha));

        const auto addr = server_ ? server_->localAddress() : "获取中...";
        const auto info = "IP: " + addr + "  端口: " + std::to_string(networkPort_);
        drawText(info, {200.0f, 270.0f + textFloat * 0.4f}, 32,
                 sf::Color(160, 200, 145, textAlphaStrong), sf::Text::Bold);

        drawText("对手连接后将自动开始对局，你执黑先手。", {200.0f, 360.0f + textFloat * 0.3f}, 20,
                 sf::Color(195, 185, 165, textAlpha));
        drawText("确保两台电脑连接在同一局域网（同一 WiFi 或同一路由器下）。", {200.0f, 400.0f + textFloat * 0.3f}, 18,
                 sf::Color(175, 162, 140, textAlpha));
    } else {
        drawText("加入房间", {88.0f, 90.0f + textFloat}, 46,
                 sf::Color(247, 241, 229, textAlphaStrong), sf::Text::Bold);
        drawText("请输入房主的 IP 地址：", {94.0f, 170.0f + textFloat * 0.6f}, 22,
                 sf::Color(200, 190, 170, textAlpha));

        // IP input box — frosted glass style with cursor
        const float ibX = 240.0f;
        const float ibY = 230.0f;
        const float ibW = 800.0f;
        const float ibH = 56.0f;
        const sf::Vector2f mousePosition = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
        const bool ibHovered = sf::FloatRect({ibX, ibY}, {ibW, ibH}).contains(mousePosition);

        // Input box shadow
        sf::RectangleShape ibShadow;
        ibShadow.setPosition({ibX + 3.0f, ibY + 4.0f});
        ibShadow.setSize({ibW, ibH});
        ibShadow.setFillColor(sf::Color(6, 4, 14, 100));
        window_.draw(ibShadow);

        // Input box glass fill
        sf::RectangleShape ibFill;
        ibFill.setPosition({ibX, ibY});
        ibFill.setSize({ibW, ibH});
        ibFill.setFillColor(ipInputActive_ ? sf::Color(30, 20, 45, 210) : sf::Color(20, 12, 30, 160));
        ibFill.setOutlineThickness(1.5f);
        ibFill.setOutlineColor(ipInputActive_
            ? sf::Color(140, 110, 180, 100)
            : (ibHovered ? sf::Color(110, 90, 150, 80) : sf::Color(85, 65, 115, 70)));
        window_.draw(ibFill);

        // Input box top bevel
        sf::RectangleShape ibHighlight;
        ibHighlight.setPosition({ibX + 2.0f, ibY + 1.0f});
        ibHighlight.setSize({ibW - 4.0f, 1.0f});
        ibHighlight.setFillColor(sf::Color(140, 115, 180, ipInputActive_ ? 55 : 30));
        window_.draw(ibHighlight);

        // IP text and blinking cursor
        if (ipInputActive_) {
            // Active: show IP text with blinking cursor at the end (with padding from left edge)
            const bool cursorOn = std::fmod(elapsed, 1.0f) < 0.55f;
            const auto displayIp = joinIp_.empty() ? "" : joinIp_;
            drawText(displayIp + (cursorOn ? "|" : ""),
                     {ibX + 20.0f, ibY + 12.0f}, 28,
                     sf::Color(230, 220, 200, textAlphaStrong));
        } else {
            // Inactive: show placeholder or IP
            const auto displayIp = joinIp_.empty() ? "在此输入 IP 地址..." : joinIp_;
            const auto ipColor = joinIp_.empty()
                ? sf::Color(100, 95, 85, textAlpha)
                : sf::Color(230, 220, 200, textAlphaStrong);
            drawText(displayIp, {ibX + 20.0f, ibY + 12.0f}, 28, ipColor);
        }

        // Status text with dots
        if (client_ && client_->isConnecting()) {
            drawText("正在连接中" + dots, {440.0f, 310.0f + textFloat * 0.5f}, 24,
                     sf::Color(220, 200, 140, textAlphaStrong), sf::Text::Bold);
        }
        if (client_ && client_->isConnectionFailed()) {
            drawText("连接失败，请检查 IP 地址是否正确", {300.0f, 310.0f + textFloat * 0.5f}, 22,
                     sf::Color(240, 120, 100, textAlphaStrong), sf::Text::Bold);
        }
    }

    // Action buttons using 4.png texture
    if (modeBtnTex_.getSize().x > 0) {
        const auto texSize = modeBtnTex_.getSize();
        const float texH = static_cast<float>(texSize.y);
        const float btnScale = 100.0f / texH;
        const float btnW = static_cast<float>(texSize.x) * btnScale;
        const float btnH = texH * btnScale;
        const sf::Vector2f mousePosition = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));

        if (isNetworkHost_) {
            // Single "取消等待" button for host
            const float bX = (1280.0f - btnW) * 0.5f;
            const float bY = 500.0f;
            const bool hovered = sf::FloatRect({bX, bY}, {btnW, btnH}).contains(mousePosition);
            const bool pressed = buttonAnimIndex_ == 403;

            const float scale = pressed ? 0.92f : 1.0f;
            const float dW = btnW * scale;
            const float dH = btnH * scale;
            const float dX = bX + (btnW - dW) * 0.5f;
            const float dY = bY + (btnH - dH) * 0.5f;

            sf::Sprite btnSpr(modeBtnTex_);
            btnSpr.setPosition({dX, dY});
            btnSpr.setScale({btnScale * scale, btnScale * scale});
            if (hovered) btnSpr.setColor(sf::Color(255, 255, 235));
            window_.draw(btnSpr);

            if (hovered && !pressed) {
                sf::RectangleShape glow;
                glow.setPosition({dX, dY});
                glow.setSize({dW, dH});
                glow.setFillColor(sf::Color(255, 255, 220, 24));
                window_.draw(glow);
            }

            drawCenteredText("取消等待", sf::FloatRect({dX, dY}, {dW, dH}),
                             static_cast<unsigned>(26.0f * scale),
                             sf::Color(252, 248, 238, 240), sf::Text::Bold);
        } else {
            const float bGap = 32.0f;
            if (!joinIp_.empty() && (!client_ || (!client_->isConnecting() && !client_->isConnected()))) {
                // Two buttons: 连接 + 取消等待
                const float totalW = btnW * 2.0f + bGap;
                const float leftX = (1280.0f - totalW) * 0.5f;
                const float bY = 500.0f;

                const struct { float x; std::string label; int idx; } btns[] = {
                    {leftX, "连接", 404},
                    {leftX + btnW + bGap, "取消等待", 403},
                };
                for (const auto& btn : btns) {
                    const bool hovered = sf::FloatRect({btn.x, bY}, {btnW, btnH}).contains(mousePosition);
                    const bool pressed = buttonAnimIndex_ == btn.idx;
                    const float scale = pressed ? 0.92f : 1.0f;
                    const float dW = btnW * scale;
                    const float dH = btnH * scale;
                    const float dX = btn.x + (btnW - dW) * 0.5f;
                    const float dY = bY + (btnH - dH) * 0.5f;

                    sf::Sprite btnSpr(modeBtnTex_);
                    btnSpr.setPosition({dX, dY});
                    btnSpr.setScale({btnScale * scale, btnScale * scale});
                    if (hovered) btnSpr.setColor(sf::Color(255, 255, 235));
                    window_.draw(btnSpr);

                    if (hovered && !pressed) {
                        sf::RectangleShape glow;
                        glow.setPosition({dX, dY});
                        glow.setSize({dW, dH});
                        glow.setFillColor(sf::Color(255, 255, 220, 24));
                        window_.draw(glow);
                    }

                    drawCenteredText(btn.label, sf::FloatRect({dX, dY}, {dW, dH}),
                                     static_cast<unsigned>(26.0f * scale),
                                     sf::Color(252, 248, 238, 240), sf::Text::Bold);
                }
            } else {
                // Single "取消等待" button
                const float bX = (1280.0f - btnW) * 0.5f;
                const float bY = 500.0f;
                const bool hovered = sf::FloatRect({bX, bY}, {btnW, btnH}).contains(mousePosition);
                const bool pressed = buttonAnimIndex_ == 403;

                const float scale = pressed ? 0.92f : 1.0f;
                const float dW = btnW * scale;
                const float dH = btnH * scale;
                const float dX = bX + (btnW - dW) * 0.5f;
                const float dY = bY + (btnH - dH) * 0.5f;

                sf::Sprite btnSpr(modeBtnTex_);
                btnSpr.setPosition({dX, dY});
                btnSpr.setScale({btnScale * scale, btnScale * scale});
                if (hovered) btnSpr.setColor(sf::Color(255, 255, 235));
                window_.draw(btnSpr);

                if (hovered && !pressed) {
                    sf::RectangleShape glow;
                    glow.setPosition({dX, dY});
                    glow.setSize({dW, dH});
                    glow.setFillColor(sf::Color(255, 255, 220, 24));
                    window_.draw(glow);
                }

                drawCenteredText("取消等待", sf::FloatRect({dX, dY}, {dW, dH}),
                                 static_cast<unsigned>(26.0f * scale),
                                 sf::Color(252, 248, 238, 240), sf::Text::Bold);
            }
        }
    }

    // Ensure networkBgTex_ is loaded for this scene
    if (networkBgTex_.getSize().x == 0) {
        for (const auto* prefix : {"assets/", "../assets/"}) {
            std::string path = std::string(prefix) + "222.png";
            if (std::filesystem::exists(path)) {
                static_cast<void>(networkBgTex_.loadFromFile(path));
                networkBgTex_.setSmooth(false);
                break;
            }
        }
    }
}

void Game::handleMainMenuClick(sf::Vector2f mousePosition) {
    if (buttonAnimIndex_ >= 0) {
        return; // Ignore clicks during animation
    }

    const float btnH = 110.0f;
    const float btnGap = 28.0f;
    const float btnStartY = 340.0f;

    const sf::Texture* texes[] = {&startBtnTex_, &rulesBtnTex_, &exitBtnTex_};

    for (int i = 0; i < 3; ++i) {
        if (texes[i]->getSize().x == 0) continue;
        const float scaleY_btn = btnH / static_cast<float>(texes[i]->getSize().y);
        const float btnW = static_cast<float>(texes[i]->getSize().x) * scaleY_btn;
        const float btnX = (1280.0f - btnW) * 0.5f;
        const float btnY = btnStartY + static_cast<float>(i) * (btnH + btnGap);

        if (sf::FloatRect({btnX, btnY}, {btnW, btnH}).contains(mousePosition)) {
            buttonAnimIndex_ = i;
            buttonAnimClock_.restart();
            return;
        }
    }
}

void Game::handleRulesClick(sf::Vector2f mousePosition) {
    // Page navigation arrows
    constexpr float cardX = 100.0f;
    constexpr float cardY = 68.0f;
    constexpr float cardW = 1080.0f;
    constexpr float cardH = 650.0f;
    constexpr float arrowY = cardY + cardH - 60.0f;

    if (rulesPage_ > 0) {
        constexpr float leftArrowX = cardX + cardW - 172.0f;
        if (sf::FloatRect({leftArrowX, arrowY}, {48.0f, 48.0f}).contains(mousePosition)) {
            --rulesPage_;
            playButtonSound();
            return;
        }
    }

    if (rulesPage_ < kRulesTotalPages - 1) {
        constexpr float rightArrowX = cardX + cardW - 108.0f;
        if (sf::FloatRect({rightArrowX, arrowY}, {48.0f, 48.0f}).contains(mousePosition)) {
            ++rulesPage_;
            playButtonSound();
            return;
        }
    }

    // Back button (4.png texture, centered, overlaps card bottom)
    if (modeBtnTex_.getSize().x > 0) {
        const float btnScale = 100.0f / static_cast<float>(modeBtnTex_.getSize().y);
        const float btnW = static_cast<float>(modeBtnTex_.getSize().x) * btnScale;
        const float btnH = 100.0f;
        const float btnX = (1280.0f - btnW) * 0.5f;
        const float btnY = 710.0f;

        if (sf::FloatRect({btnX, btnY}, {btnW, btnH}).contains(mousePosition)) {
            playButtonSound();
            scene_ = Scene::MainMenu;
            updateWindowTitle();
        }
    }
}

void Game::handleModeSelectClick(sf::Vector2f mousePosition) {
    if (buttonAnimIndex_ >= 0) return;

    const float texW = static_cast<float>(modeBtnTex_.getSize().x);
    const float texH = static_cast<float>(modeBtnTex_.getSize().y);

    const float mainScale = 120.0f / texH;
    const float mainW = texW * mainScale;
    const float mainH = texH * mainScale;

    const float colGap = 36.0f;
    const float rowGap = 22.0f;
    const float totalW = mainW * 2.0f + colGap;
    const float leftX = (1280.0f - totalW) * 0.5f;
    const float rightX = leftX + mainW + colGap;
    const float row1Y = 230.0f;
    const float row2Y = row1Y + mainH + rowGap;

    const struct { sf::FloatRect rect; int idx; } buttons[] = {
        {sf::FloatRect({leftX, row1Y}, {mainW, mainH}), 100},
        {sf::FloatRect({rightX, row1Y}, {mainW, mainH}), 101},
        {sf::FloatRect({leftX, row2Y}, {mainW, mainH}), 102},
        {sf::FloatRect({rightX, row2Y}, {mainW, mainH}), 103},
        {sf::FloatRect({(1280.0f - mainW) * 0.5f, 530.0f}, {mainW, mainH}), 104},
    };

    for (const auto& [rect, idx] : buttons) {
        if (rect.contains(mousePosition)) {
            buttonAnimIndex_ = idx;
            buttonAnimClock_.restart();
            return;
        }
    }
}

void Game::handleDifficultySelectClick(sf::Vector2f mousePosition) {
    if (buttonAnimIndex_ >= 0) return;

    constexpr float cardY = 260.0f;
    constexpr float cardWidth = 320.0f;
    constexpr float cardHeight = 280.0f;
    constexpr float cardGap = 30.0f;
    constexpr float totalWidth = cardWidth * 3.0f + cardGap * 2.0f;
    constexpr float startX = (1280.0f - totalWidth) * 0.5f;

    // Back button using 4.png uniform scale
    const float backScale = 100.0f / static_cast<float>(modeBtnTex_.getSize().y);
    const float backW = static_cast<float>(modeBtnTex_.getSize().x) * backScale;
    const float backH = static_cast<float>(modeBtnTex_.getSize().y) * backScale;

    const struct { sf::FloatRect rect; int idx; } buttons[] = {
        {sf::FloatRect({startX, cardY}, {cardWidth, cardHeight}), 200},
        {sf::FloatRect({startX + cardWidth + cardGap, cardY}, {cardWidth, cardHeight}), 201},
        {sf::FloatRect({startX + (cardWidth + cardGap) * 2.0f, cardY}, {cardWidth, cardHeight}), 202},
        {sf::FloatRect({(1280.0f - backW) * 0.5f, 610.0f}, {backW, backH}), 203},
    };

    for (const auto& [rect, idx] : buttons) {
        if (rect.contains(mousePosition)) {
            buttonAnimIndex_ = idx;
            buttonAnimClock_.restart();
            return;
        }
    }
}

void Game::handleGameClick(sf::Vector2f mousePosition) {
    // Block all game interaction during card events (Choosing handled in processEvents)
    if (cardEventState_ != CardEventState::Idle) return;

    if (networkDisconnected_) {
        if (buttonAnimIndex_ >= 0) return;
        const float dlgX = 290.0f;
        const float dlgY = 270.0f;
        const float dlgW = 700.0f;
        const float btnW = 360.0f;
        const float btnH = 64.0f;
        const float btnX = dlgX + (dlgW - btnW) * 0.5f;
        const float btnY = dlgY + 118.0f;
        if (sf::FloatRect({btnX, btnY}, {btnW, btnH}).contains(mousePosition)) {
            buttonAnimIndex_ = 310;
            buttonAnimClock_.restart();
        }
        return;
    }

    if (buttonAnimIndex_ >= 0) {
        return; // Ignore clicks during animation
    }

    if (gameReady_) {
        gameReady_ = false;
        gameIntroAlpha_ = 0.0f;
        gameIntroClock_.restart();
        turnTimer_.restart();
        return;
    }

    if (networkMode_) {
        const struct { sf::FloatRect rect; int idx; } btns[] = {
            {sf::FloatRect({790.0f, 600.0f}, {190.0f, 62.0f}), 310},
            {sf::FloatRect({1002.0f, 600.0f}, {190.0f, 62.0f}), 311},
            {sf::FloatRect({790.0f, 674.0f}, {196.0f, 62.0f}), 312},
            {sf::FloatRect({1006.0f, 674.0f}, {196.0f, 62.0f}), 313},
        };
        for (const auto& [rect, idx] : btns) {
            if (rect.contains(mousePosition)) {
                // Validate before animating
                if (idx == 312 && (turnTimedOut_ || !(remainingUndos_ > 0 && !gameOver_ && !isMyTurn()))) return;
                if (idx == 313 && gameOver_) return;
                buttonAnimIndex_ = idx;
                buttonAnimClock_.restart();
                return;
            }
        }
        tryPlacePiece({static_cast<int>(std::lround(mousePosition.x)),
                       static_cast<int>(std::lround(mousePosition.y))});
        return;
    }

    const struct { sf::FloatRect rect; int idx; } btns[] = {
        {sf::FloatRect({790.0f, 674.0f}, {402.0f, 62.0f}), 300},
        {sf::FloatRect({790.0f, 600.0f}, {190.0f, 62.0f}), 302},
        {sf::FloatRect({1002.0f, 600.0f}, {190.0f, 62.0f}), 301},
    };
    for (const auto& [rect, idx] : btns) {
        if (rect.contains(mousePosition)) {
            if (idx == 300 && (gameOver_ || turnTimedOut_ || (aiEnabled_ && remainingUndos_ == 0))) return;
            buttonAnimIndex_ = idx;
            buttonAnimClock_.restart();
            return;
        }
    }

    tryPlacePiece({static_cast<int>(std::lround(mousePosition.x)),
                   static_cast<int>(std::lround(mousePosition.y))});
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

std::optional<sf::Vector2i> Game::findEasyAiMove() const {
    std::vector<sf::Vector2i> emptyCells;
    for (int row = 0; row < Board::kBoardSize; ++row) {
        for (int col = 0; col < Board::kBoardSize; ++col) {
            if (board_.canPlaceAt(row, col)) {
                emptyCells.push_back({row, col});
            }
        }
    }

    if (emptyCells.empty()) {
        return std::nullopt;
    }

    static thread_local std::mt19937 rng(std::random_device{}());
    if (std::uniform_int_distribution<>(1, 100)(rng) <= 35) {
        return emptyCells[std::uniform_int_distribution<std::size_t>(0, emptyCells.size() - 1)(rng)];
    }

    int bestScore = std::numeric_limits<int>::min();
    std::optional<sf::Vector2i> bestMove;

    for (const auto& cell : emptyCells) {
        const int offense = scoreMove(cell.x, cell.y, Board::Piece::White);
        const int centerBias = 14 - (std::abs(cell.x - 7) + std::abs(cell.y - 7));
        const int total = offense + centerBias;

        if (total > bestScore) {
            bestScore = total;
            bestMove = cell;
        }
    }

    return bestMove;
}

std::optional<sf::Vector2i> Game::findHardAiMove() const {
    struct Candidate {
        sf::Vector2i pos;
        int score;
    };

    std::vector<Candidate> candidates;
    for (int row = 0; row < Board::kBoardSize; ++row) {
        for (int col = 0; col < Board::kBoardSize; ++col) {
            if (!board_.canPlaceAt(row, col)) {
                continue;
            }

            const int offense = scoreMove(row, col, Board::Piece::White);
            const int defense = scoreMove(row, col, Board::Piece::Black);
            const int centerBias = 14 - (std::abs(row - 7) + std::abs(col - 7));

            int forkBonus = 0;
            int openThrees = 0;
            const std::array<std::pair<int, int>, 4> dirs = {{{0, 1}, {1, 0}, {1, 1}, {1, -1}}};
            for (const auto& [dRow, dCol] : dirs) {
                const int fwd = countInDirection(row, col, dRow, dCol, Board::Piece::White);
                const int bwd = countInDirection(row, col, -dRow, -dCol, Board::Piece::White);
                const bool ofwd = isOpenEnd(row + (fwd + 1) * dRow, col + (fwd + 1) * dCol);
                const bool obwd = isOpenEnd(row - (bwd + 1) * dRow, col - (bwd + 1) * dCol);
                if (fwd + bwd == 3 && static_cast<int>(ofwd) + static_cast<int>(obwd) == 2) {
                    ++openThrees;
                }
            }
            if (openThrees >= 2) {
                forkBonus = 15000;
            } else if (openThrees == 1) {
                forkBonus = 3000;
            }

            const int total = offense * 2 + defense * 2 + centerBias + forkBonus;
            candidates.push_back({{row, col}, total});
        }
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    const std::size_t lookCount = std::min<std::size_t>(8, candidates.size());
    int bestNet = std::numeric_limits<int>::min();
    std::optional<sf::Vector2i> bestMove;

    for (std::size_t i = 0; i < lookCount; ++i) {
        const int ourScore = candidates[i].score;
        int opponentBest = 0;
        for (std::size_t j = 0; j < lookCount; ++j) {
            if (i == j) {
                continue;
            }
            const int oppScore = scoreMove(candidates[j].pos.x, candidates[j].pos.y,
                                           Board::Piece::Black);
            if (oppScore > opponentBest) {
                opponentBest = oppScore;
            }
        }

        const int net = ourScore - opponentBest;
        if (net > bestNet) {
            bestNet = net;
            bestMove = candidates[i].pos;
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
    static constexpr std::array<const char*, 2> pixelPaths = {
        "assets/fusion-pixel-12px-proportional-zh_hans.ttf",
        "../assets/fusion-pixel-12px-proportional-zh_hans.ttf"
    };
    for (const auto* path : pixelPaths) {
        if (std::filesystem::exists(path) && uiFont_.openFromFile(path)) {
            return true;
        }
    }

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

void Game::loadMenuBackground() {
    static constexpr std::array<const char*, 2> prefixes = {
        "assets/", "../assets/"
    };

    auto loadTex = [&](sf::Texture& tex, const char* filename) {
        for (const auto* prefix : prefixes) {
            std::string path = std::string(prefix) + filename;
            if (std::filesystem::exists(path)) {
                static_cast<void>(tex.loadFromFile(path));
                tex.setSmooth(false);
                return;
            }
        }
    };

    loadTex(menuBgTex_, "222.png");
    loadTex(startBtnTex_, "1.png");
    loadTex(rulesBtnTex_, "2.png");
    loadTex(exitBtnTex_, "3.png");
    loadTex(modeBtnTex_, "4.png");
    loadTex(diffCardTex_, "5.png");

    // Generate noise texture for blur overlay
    {
        constexpr int ns = 64;
        sf::Image img({ns, ns});
        for (int y = 0; y < ns; ++y) {
            for (int x = 0; x < ns; ++x) {
                const int n = (std::rand() % 31) - 15;
                const auto v = static_cast<std::uint8_t>(128 + n);
                img.setPixel({static_cast<unsigned>(x), static_cast<unsigned>(y)}, sf::Color(v, v, v, 255));
            }
        }
        static_cast<void>(noiseTex_.loadFromImage(img));
        noiseTex_.setSmooth(false);
        noiseTex_.setRepeated(true);
    }
}

void Game::updateTitleParticles() {
    // Spawn new particles
    const float elapsed = titleClock_.getElapsedTime().asSeconds();
    static float lastSpawnTime = 0.0f;
    if (titleParticles_.size() < 40 && elapsed - lastSpawnTime > 0.22f) {
        lastSpawnTime = elapsed;
        TitleParticle p;
        p.pos = {static_cast<float>(std::rand() % 1000 + 140), -16.0f};
        p.vel = {0.0f, 18.0f + static_cast<float>(std::rand() % 20)};
        p.maxLife = 5.0f + static_cast<float>(std::rand() % 5);
        p.life = p.maxLife;
        p.rotSpeed = (static_cast<float>(std::rand() % 80) - 40.0f) * 0.5f;
        p.swayPhase = static_cast<float>(std::rand() % 628) * 0.01f;
        p.swayAmp = 25.0f + static_cast<float>(std::rand() % 30);
        static const sf::Color leafColors[] = {
            {210, 140, 50}, {190, 120, 40}, {220, 160, 60},
            {180, 100, 35}, {200, 150, 55}, {170, 130, 45},
            {160, 95, 30}, {195, 145, 48}
        };
        p.color = leafColors[std::rand() % 8];
        titleParticles_.push_back(p);
    }

    // Update existing particles
    for (auto& p : titleParticles_) {
        const float dt = 1.0f / 60.0f;
        p.life -= dt;
        p.pos.x += std::sin(p.swayPhase + elapsed * 1.5f) * p.swayAmp * dt;
        p.pos.y += p.vel.y * dt;
        p.rotation += p.rotSpeed * dt;
    }

    // Remove dead particles
    titleParticles_.erase(
        std::remove_if(titleParticles_.begin(), titleParticles_.end(),
                       [](const TitleParticle& p) { return p.life <= 0.0f || p.pos.y > 850.0f; }),
        titleParticles_.end());
}

void Game::updateGameParticles() {
    if (scene_ != Scene::Playing) return;

    const float elapsed = gameParticleClock_.getElapsedTime().asSeconds();
    const float dt = 1.0f / 60.0f;

    float spawnInterval;
    int maxParticles;
    switch (aiDifficulty_) {
    case AIDifficulty::Easy:   spawnInterval = 0.22f; maxParticles = 50; break;
    case AIDifficulty::Medium: spawnInterval = 0.35f; maxParticles = 40; break;
    case AIDifficulty::Hard:   spawnInterval = 0.15f; maxParticles = 55; break;
    }

    // Spawn
    if (static_cast<int>(gameParticles_.size()) < maxParticles && elapsed - gameParticleTimer_ > spawnInterval) {
        gameParticleTimer_ = elapsed;
        GameParticle p;
        switch (aiDifficulty_) {
        case AIDifficulty::Easy: {
            // Cherry blossom petals — drift down from top, full width
            p.pos = {-30.0f + static_cast<float>(std::rand() % 1340), -20.0f};
            p.vel = {(static_cast<float>(std::rand() % 10) - 5.0f) * 0.6f,
                     12.0f + static_cast<float>(std::rand() % 18)};
            p.maxLife = 7.0f + static_cast<float>(std::rand() % 6);
            p.swayPhase = static_cast<float>(std::rand() % 628) * 0.01f;
            p.swayAmp = 20.0f + static_cast<float>(std::rand() % 20);
            p.size = 3.0f + static_cast<float>(std::rand() % 5);
            static const sf::Color petalColors[] = {
                {255, 190, 200}, {255, 210, 220}, {255, 175, 190},
                {250, 200, 210}, {255, 220, 230}, {245, 185, 195},
                {255, 200, 215}, {250, 215, 225}
            };
            p.color = petalColors[std::rand() % 8];
            break;
        }
        case AIDifficulty::Medium: {
            // Fireflies — float across full screen
            p.pos = {-20.0f + static_cast<float>(std::rand() % 1320),
                     100.0f + static_cast<float>(std::rand() % 620)};
            p.vel = {(static_cast<float>(std::rand() % 24) - 12.0f) * 0.7f,
                     (static_cast<float>(std::rand() % 20) - 10.0f) * 0.7f};
            p.maxLife = 5.0f + static_cast<float>(std::rand() % 6);
            p.swayPhase = static_cast<float>(std::rand() % 628) * 0.01f;
            p.swayAmp = 10.0f + static_cast<float>(std::rand() % 12);
            p.alphaPhase = static_cast<float>(std::rand() % 628) * 0.01f;
            p.size = 4.0f + static_cast<float>(std::rand() % 5);
            static const sf::Color fireflyColors[] = {
                {255, 220, 90}, {255, 235, 140}, {255, 210, 70},
                {255, 240, 160}, {255, 225, 100}, {255, 230, 120}
            };
            p.color = fireflyColors[std::rand() % 6];
            break;
        }
        case AIDifficulty::Hard: {
            // Embers — rise up from full width bottom with chaotic sway
            p.pos = {-20.0f + static_cast<float>(std::rand() % 1320),
                     480.0f + static_cast<float>(std::rand() % 300)};
            p.vel = {(static_cast<float>(std::rand() % 10) - 5.0f) * 0.8f,
                     -(25.0f + static_cast<float>(std::rand() % 25))};
            p.maxLife = 3.5f + static_cast<float>(std::rand() % 5);
            p.swayPhase = static_cast<float>(std::rand() % 628) * 0.01f;
            p.swayAmp = 25.0f + static_cast<float>(std::rand() % 30);
            p.size = 2.0f + static_cast<float>(std::rand() % 5);
            static const sf::Color emberColors[] = {
                {210, 80, 25}, {190, 55, 15}, {180, 70, 30},
                {200, 65, 20}, {170, 50, 10}, {195, 75, 28},
                {160, 45, 12}, {185, 60, 22}
            };
            p.color = emberColors[std::rand() % 8];
            break;
        }
        }
        p.life = p.maxLife;
        gameParticles_.push_back(p);
    }

    // Update
    for (auto& p : gameParticles_) {
        p.life -= dt;
        if (aiDifficulty_ == AIDifficulty::Hard) {
            // Embers: chaotic sway + upward drift
            p.pos.x += std::sin(p.swayPhase + elapsed * 3.5f) * p.swayAmp * dt;
            p.pos.y += p.vel.y * dt;
            // Embers slow down as they rise
            p.vel.y += 2.0f * dt;
        } else if (aiDifficulty_ == AIDifficulty::Medium) {
            // Fireflies: gentle random float
            p.pos.x += std::cos(p.swayPhase + elapsed * 1.2f) * p.swayAmp * dt;
            p.pos.y += std::sin(p.swayPhase * 1.7f + elapsed * 1.8f) * p.swayAmp * 0.7f * dt;
            p.pos += p.vel * dt;
            // Bounce off screen edges loosely
            if (p.pos.x < -10.0f) p.vel.x = std::abs(p.vel.x);
            if (p.pos.x > 1290.0f) p.vel.x = -std::abs(p.vel.x);
            if (p.pos.y < 50.0f) p.vel.y = std::abs(p.vel.y);
            if (p.pos.y > 770.0f) p.vel.y = -std::abs(p.vel.y);
        } else {
            // Petals: gentle downward drift with sway
            p.pos.x += std::sin(p.swayPhase + elapsed * 1.5f) * p.swayAmp * dt;
            p.pos.y += p.vel.y * dt;
            p.vel.y += 1.5f * dt; // slight acceleration
        }
    }

    // Remove dead particles
    gameParticles_.erase(
        std::remove_if(gameParticles_.begin(), gameParticles_.end(),
                       [](const GameParticle& p) {
                           return p.life <= 0.0f || p.pos.y < -60.0f ||
                                  p.pos.y > 880.0f || p.pos.x < -60.0f || p.pos.x > 1340.0f;
                       }),
        gameParticles_.end());
}

void Game::updateSparks() {
    const float dt = 1.0f / 60.0f;
    for (auto& s : sparks_) {
        s.life -= dt;
        s.pos += s.vel * dt;
        s.vel *= 0.92f; // friction
    }
    sparks_.erase(
        std::remove_if(sparks_.begin(), sparks_.end(),
                       [](const Spark& s) { return s.life <= 0.0f; }),
        sparks_.end());
}

void Game::spawnSparks(sf::Vector2f pos, Board::Piece /*piece*/) {
    const int count = 8 + std::rand() % 6;
    static const sf::Color sparkColors[] = {
        {255, 215, 70}, {255, 240, 150}, {245, 195, 45},
        {255, 225, 110}, {250, 205, 60}, {255, 245, 170},
        {240, 185, 50}, {255, 230, 130}
    };
    for (int i = 0; i < count; ++i) {
        Spark s;
        s.pos = pos;
        const float angle = static_cast<float>(std::rand() % 628) * 0.01f;
        const float speed = 55.0f + static_cast<float>(std::rand() % 60);
        s.vel = {std::cos(angle) * speed, std::sin(angle) * speed};
        s.maxLife = 0.32f + static_cast<float>(std::rand() % 20) * 0.01f;
        s.life = s.maxLife;
        s.size = 4.0f + static_cast<float>(std::rand() % 4);
        s.color = sparkColors[std::rand() % 8];
        sparks_.push_back(s);
    }
}

void Game::drawMainMenu() {
    // Background with mouse parallax
    if (menuBgTex_.getSize().x > 0) {
        const auto texSize = menuBgTex_.getSize();
        const float margin = 1.08f;
        const float scaleX = 1280.0f / static_cast<float>(texSize.x) * margin;
        const float scaleY = 820.0f / static_cast<float>(texSize.y) * margin;

        const sf::Vector2i mousePos = sf::Mouse::getPosition(window_);
        const float paraX = (static_cast<float>(mousePos.x) - 640.0f) / 640.0f * 18.0f;
        const float paraY = (static_cast<float>(mousePos.y) - 410.0f) / 410.0f * 14.0f;

        sf::Sprite bgSpr(menuBgTex_);
        bgSpr.setScale({scaleX, scaleY});
        bgSpr.setPosition({-50.0f - paraX, -22.0f - paraY});
        window_.draw(bgSpr);
    }

    // Title with drop shadow — centered, animated
    const auto titleText = "幻格五子棋";
    const unsigned titleSize = 80;

    // Slow vertical float (±4px, ~3.5s cycle)
    const float floatOffset = std::sin(titleClock_.getElapsedTime().asSeconds() * 1.8f) * 4.0f;
    const float titleY = 72.0f + floatOffset;

    const sf::FloatRect titleArea({0.0f, titleY}, {1280.0f, 100.0f});
    const sf::FloatRect shadowArea({5.0f, titleY + 5.0f}, {1280.0f, 100.0f});
    const sf::Color shadowColor(32, 20, 12, 200);
    const sf::Color titleColor(252, 232, 180);

    drawCenteredText(titleText, shadowArea, titleSize, shadowColor, sf::Text::Bold);
    drawCenteredText(titleText, titleArea, titleSize, titleColor, sf::Text::Bold);

    // DEMO badge — dual-color + glitch jitter (beside title bottom-right)
    {
        const auto demoText = "DEMO";
        const unsigned demoSize = 40;
        // Title is centered at Y≈72+float, size 80, width ~240px
        const float actualDemoX = 850.0f;
        const float demoBaseY = titleY + 80.0f;

        // Glitch: random micro-offset every ~150ms
        const float glitchTime = titleClock_.getElapsedTime().asSeconds();
        static float lastGlitchTime = 0.0f;
        static float glitchX = 0.0f;
        static float glitchY = 0.0f;
        if (glitchTime - lastGlitchTime > 0.15f) {
            lastGlitchTime = glitchTime;
            glitchX = static_cast<float>(std::rand() % 5 - 2);
            glitchY = static_cast<float>(std::rand() % 5 - 2);
        }

        const float dx = actualDemoX + glitchX;
        const float dy = demoBaseY + glitchY;

        // Dark offset layer (3D pixel shadow)
        drawText(demoText, {dx + 4.0f, dy - 2.0f}, demoSize, sf::Color(48, 18, 18, 240), sf::Text::Bold);
        // Main bright layer
        drawText(demoText, {dx, dy}, demoSize, sf::Color(255, 225, 120), sf::Text::Bold);
    }

    // Buttons — image-based, centered
    const float btnH = 110.0f;
    const float btnGap = 28.0f;
    const float btnStartY = 340.0f;
    const sf::Vector2f mousePos = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));

    struct BtnDef {
        sf::Texture& tex;
        std::string label;
        int index;
    };
    const BtnDef buttons[] = {
        {startBtnTex_, "开始游戏", 0},
        {rulesBtnTex_, "操作规则说明", 1},
        {exitBtnTex_, "退出游戏", 2}
    };

    for (const auto& [tex, label, idx] : buttons) {
        if (tex.getSize().x == 0) continue;

        const float scaleY_btn = btnH / static_cast<float>(tex.getSize().y);
        const float btnW = static_cast<float>(tex.getSize().x) * scaleY_btn;
        const float btnX = (1280.0f - btnW) * 0.5f;
        const float btnY = btnStartY + static_cast<float>(idx) * (btnH + btnGap);

        const bool hovered = sf::FloatRect({btnX, btnY}, {btnW, btnH}).contains(mousePos);
        const bool pressed = buttonAnimIndex_ == idx;

        const float scale = pressed ? 0.92f : 1.0f;
        const float drawW = btnW * scale;
        const float drawH = btnH * scale;
        const float drawX = btnX + (btnW - drawW) * 0.5f;
        const float drawY = btnY + (btnH - drawH) * 0.5f;

        // Button sprite
        sf::Sprite btnSpr(tex);
        btnSpr.setPosition({drawX, drawY});
        btnSpr.setScale({scaleY_btn * scale, scaleY_btn * scale});
        if (hovered) {
            btnSpr.setColor(sf::Color(255, 255, 235));
        }
        window_.draw(btnSpr);

        // Text overlay
        drawCenteredText(label, sf::FloatRect({drawX, drawY}, {drawW, drawH}),
                         static_cast<unsigned>(32.0f * scale),
                         sf::Color(252, 248, 238, 240), sf::Text::Bold);

        // Hover glow
        if (hovered) {
            sf::RectangleShape glow;
            glow.setPosition({drawX, drawY});
            glow.setSize({drawW, drawH});
            glow.setFillColor(sf::Color(255, 255, 220, 24));
            window_.draw(glow);
        }
    }

    // Leaf particles
    for (const auto& p : titleParticles_) {
        const float alpha = p.life / p.maxLife;
        sf::RectangleShape leaf({10.0f, 5.0f});
        leaf.setOrigin({5.0f, 2.5f});
        leaf.setPosition(p.pos);
        leaf.setRotation(sf::degrees(p.rotation));
        leaf.setFillColor(sf::Color(p.color.r, p.color.g, p.color.b,
                                    static_cast<std::uint8_t>(alpha * 200.0f)));
        window_.draw(leaf);
    }
}

void Game::drawRulesScene() {
    // Load background texture on first use
    if (rulesBgTex_.getSize().x == 0) {
        for (const auto* prefix : {"assets/", "../assets/"}) {
            const std::string path = std::string(prefix) + std::string("222.png");
            if (std::filesystem::exists(path)) {
                static_cast<void>(rulesBgTex_.loadFromFile(path));
                rulesBgTex_.setSmooth(true);
                break;
            }
        }
    }

    const float rulesTime = modeSelectClock_.getElapsedTime().asSeconds();

    // --- Animated background ---
    if (rulesBgTex_.getSize().x > 0) {
        const auto texSize = rulesBgTex_.getSize();
        const float scaleX = 1280.0f / static_cast<float>(texSize.x);
        const float scaleY = 820.0f / static_cast<float>(texSize.y);
        const float baseScale = std::max(scaleX, scaleY) * 1.15f;

        // Slow zoom pulse ±3%
        const float zoom = 1.0f + std::sin(rulesTime * 0.35f) * 0.03f;

        // Slow drift
        const float driftX = std::sin(rulesTime * 0.2f) * 18.0f;
        const float driftY = std::cos(rulesTime * 0.25f) * 12.0f;

        sf::Sprite bgSpr(rulesBgTex_);
        const auto bgSize = sf::Vector2f(texSize);
        bgSpr.setOrigin({bgSize.x * 0.5f, bgSize.y * 0.5f});
        bgSpr.setPosition({640.0f + driftX, 410.0f + driftY});
        bgSpr.setScale({baseScale * zoom, baseScale * zoom});
        bgSpr.setColor(sf::Color(210, 195, 170, 255));
        window_.draw(bgSpr);
    } else {
        // Fallback dark gradient
        sf::RectangleShape fallback({1280.0f, 820.0f});
        fallback.setFillColor(sf::Color(22, 20, 28));
        window_.draw(fallback);
    }

    // Dark overlay for text readability (breathing)
    const float overlayAlpha = 0.52f + std::sin(rulesTime * 0.4f) * 0.04f;
    sf::RectangleShape overlay({1280.0f, 820.0f});
    overlay.setFillColor(sf::Color(12, 10, 20,
        static_cast<std::uint8_t>(overlayAlpha * 255.0f)));
    window_.draw(overlay);

    // Floating ambient particles (dust motes)
    {
        constexpr int kMoteCount = 18;
        for (int i = 0; i < kMoteCount; ++i) {
            const float phase = static_cast<float>(i) * 1.7f;
            const float id = static_cast<float>(i);
            const float x = 60.0f + std::fmod(id * 137.0f + std::sin(rulesTime * 0.3f + phase) * 130.0f
                + std::cos(rulesTime * 0.45f + phase * 1.5f) * 70.0f, 1160.0f);
            const float y = 820.0f - std::fmod(rulesTime * (6.0f + std::fmod(id * 2.7f, 7.0f)) + id * 67.0f, 900.0f);
            const float alpha = 0.12f + std::sin(rulesTime * 0.7f + phase) * 0.08f;
            if (alpha < 0.08f) continue;
            sf::CircleShape mote(2.0f);
            mote.setOrigin({2.0f, 2.0f});
            mote.setPosition({x, y});
            mote.setFillColor(sf::Color(220, 200, 140, static_cast<std::uint8_t>(alpha * 255.0f)));
            window_.draw(mote);
        }
    }

    // --- Content card (frosted glass) ---
    constexpr float cardX = 100.0f;
    constexpr float cardY = 68.0f;
    constexpr float cardW = 1080.0f;
    constexpr float cardH = 650.0f;

    // Card shadow
    sf::RectangleShape cardShadow({cardW + 8.0f, cardH + 8.0f});
    cardShadow.setPosition({cardX - 4.0f, cardY + 4.0f});
    cardShadow.setFillColor(sf::Color(0, 0, 0, 60));
    window_.draw(cardShadow);

    // Frosted glass base — semi-transparent white
    sf::RectangleShape card({cardW, cardH});
    card.setPosition({cardX, cardY});
    card.setFillColor(sf::Color(248, 246, 240, 185));
    window_.draw(card);

    // Frosted glass grain — tiled noise overlay
    if (noiseTex_.getSize().x > 0) {
        sf::Sprite noiseSpr(noiseTex_);
        const float nScaleX = cardW / static_cast<float>(noiseTex_.getSize().x);
        const float nScaleY = cardH / static_cast<float>(noiseTex_.getSize().y);
        noiseSpr.setPosition({cardX, cardY});
        noiseSpr.setScale({nScaleX, nScaleY});
        noiseSpr.setColor(sf::Color(255, 255, 255, 20));
        window_.draw(noiseSpr);
    }

    // Subtle white border with soft glow
    sf::RectangleShape cardBorder({cardW, cardH});
    cardBorder.setPosition({cardX, cardY});
    cardBorder.setFillColor(sf::Color::Transparent);
    cardBorder.setOutlineThickness(1.5f);
    cardBorder.setOutlineColor(sf::Color(220, 215, 200, 100));
    window_.draw(cardBorder);

    // Outer soft glow ring
    sf::RectangleShape cardGlow({cardW + 4.0f, cardH + 4.0f});
    cardGlow.setPosition({cardX - 2.0f, cardY - 2.0f});
    cardGlow.setFillColor(sf::Color::Transparent);
    cardGlow.setOutlineThickness(3.0f);
    cardGlow.setOutlineColor(sf::Color(255, 255, 255, 30));
    window_.draw(cardGlow);

    // Top decorative line
    sf::RectangleShape topLine({cardW - 40.0f, 2.0f});
    topLine.setPosition({cardX + 20.0f, cardY + 52.0f});
    topLine.setFillColor(sf::Color(180, 175, 160, 90));
    window_.draw(topLine);

    // Title
    drawText("操作规则说明", {cardX + 36.0f, cardY + 14.0f}, 36, sf::Color(42, 54, 82), sf::Text::Bold);

    // --- Page content ---
    const float contentX = cardX + 44.0f;
    const float contentY = cardY + 72.0f;
    const float lineHeight = 28.0f;
    const auto titleColor = sf::Color(52, 48, 72);
    const auto bodyColor = sf::Color(72, 58, 32);
    const auto noteColor = sf::Color(110, 90, 55);

    auto drawSection = [&](const std::string& title, float& y) {
        drawText(title, {contentX, y}, 23, titleColor, sf::Text::Bold);
        y += lineHeight + 2.0f;
    };

    auto drawBody = [&](const std::string& text, float& y, int size = 20) {
        drawText(text, {contentX + 12.0f, y}, size, bodyColor);
        y += static_cast<float>(size) * 1.35f;
    };

    auto drawNote = [&](const std::string& text, float& y, int size = 17) {
        drawText(text, {contentX + 24.0f, y}, size, noteColor);
        y += static_cast<float>(size) * 1.3f;
    };

    float y = contentY;

    if (rulesPage_ == 0) {
        // Page 1: Basic Rules & Game Modes
        drawSection("基础规则", y);
        drawBody("黑棋先手，白棋后手。在 15x15 棋盘上，先将五枚同色棋子连成", y);
        drawBody("一线（横、竖、斜均可）的一方获胜。", y);
        y += 6.0f;

        drawSection("游戏模式", y);
        drawBody("经典模式 — 标准棋盘，无障碍物。纯靠棋力较量。", y);
        drawBody("幻格模式 — 棋盘上随机生成障碍格（红色方块）。障碍格不可落子，", y);
        drawBody("增加策略深度与变数。简单/中等难度障碍物固定；困难难度障碍物", y);
        drawBody("随棋局动态增减。", y);
        y += 6.0f;

        drawSection("对战方式", y);
        drawBody("人机对战 — 你执黑先手 vs AI 执白后手。可选简单/中等/困难", y);
        drawBody("三种难度。高难度 AI 能预读多步并评估棋型。", y);
        drawBody("网络对战 — 房主执黑先手，访客执白后手。双方轮流落子，", y);
        drawBody("由服务端同步棋盘状态。支持悔棋请求、投降。", y);
        y += 6.0f;

        drawSection("悔棋与超时", y);
        drawBody("每局拥有 N 次悔棋机会（N 在房间设置中设定，0~5）。", y);
        drawBody("人机模式悔棋撤回你与 AI 各一步；网络模式悔棋需对方同意。", y);
        drawBody("每回合有时间限制（默认 8 秒），超时自动判负。落子后计时重置。", y);
        drawBody("悔棋消耗次数，次数用完后本局无法再悔棋。", y);
    } else if (rulesPage_ == 1) {
        // Page 2: Card Event System
        drawSection("卡牌事件系统（幻格模式专属）", y);
        drawBody("当棋盘上棋子总数达到阈值（10, 18, 26, 34...）时触发卡牌事件。", y);
        y += 4.0f;

        drawSection("事件流程", y);
        drawBody("征兆显现 → 命运使者降临 → 命运之轮转动 → 决定抽卡人选 →", y);
        drawBody("三张卡牌发放 → 选中者挑选一张 → 卡牌效果生效 → 继续对局", y);
        y += 4.0f;

        drawSection("命运之轮", y);
        drawBody("转盘分为「房主」与「访客」两个区域。指针停止时指向谁，", y);
        drawBody("就由谁从三张卡牌中挑选一张。转盘结果由服务端权威决定。", y);
        y += 4.0f;

        drawSection("卡牌类型（22种塔罗牌）", y);
        drawBody("棋子操作类：愚者（自动落子）、女皇（生成棋子）、魔术师（镜像）、", y);
        drawBody("恋人（交换棋子）、战车（推离棋子）、倒吊人（献祭）、死神（清除）、", y);
        drawBody("恶魔（移除棋子）、正义（平衡消除）", y);
        drawNote("障碍物类：皇帝、命运之轮、塔、太阳、审判 — 移除/重分布障碍物", y);
        drawNote("规则/辅助类：女祭司（十字清除）、教皇（9x9 限制）、力量（坚不可摧）、", y);
        drawNote("隐者（时间翻倍）、节制（无视障碍）、星星（AI 推荐）、月亮（偏移落子）、", y);
        drawNote("世界（取消悔棋）", y);
        drawNote("注：星星与力量的视觉效果仅抽卡者可见（策略隐藏）。", y);
    } else {
        // Page 3: Controls & Settings
        drawSection("操作说明", y);
        drawBody("鼠标左键 — 点击棋盘空格落子（网络模式仅自己回合有效）", y);
        drawBody("R 键 — 悔棋（撤回上一步；网络模式发送悔棋请求给对方）", y, 20);
        drawBody("F 键 — 投降认输（网络模式发送投降信号）", y, 20);
        drawBody("Esc 键 — 返回上层菜单 / 退出当前界面", y, 20);
        y += 6.0f;

        drawSection("网络对战说明", y);
        drawBody("创建房间（房主）：选择棋盘地图 → 设置悔棋次数/回合时间/障碍动态 →", y);
        drawBody("等待对方连接。本机 IP 地址将显示在屏幕上供访客输入。", y);
        drawBody("加入房间（访客）：输入房主 IP 地址 → 点击连接 → 等待同步。", y);
        drawBody("网络断开时双方会收到提示，可返回主菜单重新开始。", y);
        y += 6.0f;

        drawSection("房间设置", y);
        drawBody("悔棋次数：0~5 次（默认 3）。决定每局双方可悔棋的总次数。", y);
        drawBody("回合时间：3~15 秒（默认 8）。超时未落子自动判负。", y);
        drawBody("障碍物动态：关闭 → 障碍物固定；开启 → 困难模式障碍物随棋局增减。", y);
        drawBody("棋盘地图：从 maps/ 目录中选择自定义地图文件（.map）。", y);
        y += 6.0f;

        drawSection("提示", y);
        drawBody("对战界面右侧信息栏显示当前回合、剩余悔棋次数、倒计时等。", y);
        drawBody("人机模式中对手（AI）上方的对话框会显示当前状态与台词。", y);
    }

    // --- Page indicator ---
    const std::string pageStr = "第 " + std::to_string(rulesPage_ + 1) + " 页 / 共 " + std::to_string(kRulesTotalPages) + " 页";
    const auto pageUtf8 = sf::String::fromUtf8(pageStr.begin(), pageStr.end());
    sf::Text pageText(uiFont_, pageUtf8, 18);
    pageText.setFillColor(sf::Color(110, 90, 55));
    const auto pb = pageText.getLocalBounds();
    pageText.setPosition({cardX + (cardW - pb.size.x) * 0.5f, cardY + cardH - 42.0f});
    window_.draw(pageText);

    // Page indicator dots
    {
        constexpr float dotRadius = 5.0f;
        constexpr float dotSpacing = 20.0f;
        constexpr float dotY = cardY + cardH - 44.0f;
        constexpr float dotStartX = cardX + cardW * 0.5f - dotSpacing * (kRulesTotalPages - 1) * 0.5f;
        for (int i = 0; i < kRulesTotalPages; ++i) {
            sf::CircleShape dot(dotRadius);
            dot.setOrigin({dotRadius, dotRadius});
            dot.setPosition({dotStartX + static_cast<float>(i) * dotSpacing, dotY});
            dot.setFillColor(i == rulesPage_ ? sf::Color(160, 120, 60, 220) : sf::Color(180, 170, 150, 100));
            window_.draw(dot);
        }
    }

    // --- Navigation arrows (bottom-right corner) ---
    const sf::Vector2f mousePos = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
    constexpr float arrowY = cardY + cardH - 60.0f;

    // Left arrow (previous page)
    if (rulesPage_ > 0) {
        constexpr float leftArrowX = cardX + cardW - 172.0f;
        sf::FloatRect leftArrowRect({leftArrowX, arrowY}, {48.0f, 48.0f});
        bool hovered = leftArrowRect.contains(mousePos);

        sf::VertexArray leftArrow(sf::PrimitiveType::Triangles);
        leftArrow.append({{leftArrowX + 12.0f, arrowY + 24.0f},
                          hovered ? sf::Color(120, 90, 40, 240) : sf::Color(140, 110, 60, 200)});
        leftArrow.append({{leftArrowX + 38.0f, arrowY + 6.0f},
                          hovered ? sf::Color(120, 90, 40, 240) : sf::Color(140, 110, 60, 200)});
        leftArrow.append({{leftArrowX + 38.0f, arrowY + 42.0f},
                          hovered ? sf::Color(120, 90, 40, 240) : sf::Color(140, 110, 60, 200)});
        window_.draw(leftArrow);

        if (hovered) {
            sf::CircleShape glow(18.0f);
            glow.setOrigin({18.0f, 18.0f});
            glow.setPosition({leftArrowX + 24.0f, arrowY + 24.0f});
            glow.setFillColor(sf::Color(200, 180, 120, 40));
            window_.draw(glow);
        }
    }

    // Right arrow (next page)
    if (rulesPage_ < kRulesTotalPages - 1) {
        constexpr float rightArrowX = cardX + cardW - 108.0f;
        sf::FloatRect rightArrowRect({rightArrowX, arrowY}, {48.0f, 48.0f});
        bool hovered = rightArrowRect.contains(mousePos);

        sf::VertexArray rightArrow(sf::PrimitiveType::Triangles);
        rightArrow.append({{rightArrowX + 36.0f, arrowY + 24.0f},
                           hovered ? sf::Color(120, 90, 40, 240) : sf::Color(140, 110, 60, 200)});
        rightArrow.append({{rightArrowX + 10.0f, arrowY + 6.0f},
                           hovered ? sf::Color(120, 90, 40, 240) : sf::Color(140, 110, 60, 200)});
        rightArrow.append({{rightArrowX + 10.0f, arrowY + 42.0f},
                           hovered ? sf::Color(120, 90, 40, 240) : sf::Color(140, 110, 60, 200)});
        window_.draw(rightArrow);

        if (hovered) {
            sf::CircleShape glow(18.0f);
            glow.setOrigin({18.0f, 18.0f});
            glow.setPosition({rightArrowX + 24.0f, arrowY + 24.0f});
            glow.setFillColor(sf::Color(200, 180, 120, 40));
            window_.draw(glow);
        }
    }

    // --- Back button (4.png texture, overlaps card bottom) ---
    if (modeBtnTex_.getSize().x > 0) {
        const auto texSize = modeBtnTex_.getSize();
        const float texH = static_cast<float>(texSize.y);
        const float texW = static_cast<float>(texSize.x);
        const float btnScale = 100.0f / texH;
        const float btnW = texW * btnScale;
        const float btnH = texH * btnScale;
        const float btnX = (1280.0f - btnW) * 0.5f;
        const float btnY = 710.0f;

        const sf::FloatRect btnRect({btnX, btnY}, {btnW, btnH});
        const bool hovered = btnRect.contains(mousePos);
        const bool pressed = buttonAnimIndex_ == 999;

        // Subtle pulse animation
        const float pulse = 1.0f + std::sin(rulesTime * 0.9f) * 0.018f;
        const float scale = (pressed ? 0.92f : 1.0f) * pulse;
        const float drawW = btnW * scale;
        const float drawH = btnH * scale;
        const float drawX = btnX + (btnW - drawW) * 0.5f;
        const float drawY = btnY + (btnH - drawH) * 0.5f;
        const float sprScale = btnScale * scale;

        // Button shadow
        sf::RectangleShape btnShadow;
        btnShadow.setPosition({drawX + 3.0f, drawY + 4.0f});
        btnShadow.setSize({drawW, drawH});
        btnShadow.setFillColor(sf::Color(0, 0, 0, hovered ? 70 : 45));
        window_.draw(btnShadow);

        // Button sprite from 4.png
        sf::Sprite btnSpr(modeBtnTex_);
        btnSpr.setPosition({drawX, drawY});
        btnSpr.setScale({sprScale, sprScale});
        if (hovered) {
            btnSpr.setColor(sf::Color(255, 255, 235));
        }
        window_.draw(btnSpr);

        // Hover glow
        if (hovered && !pressed) {
            sf::RectangleShape glow;
            glow.setPosition({drawX, drawY});
            glow.setSize({drawW, drawH});
            glow.setFillColor(sf::Color(255, 255, 220, 28));
            window_.draw(glow);

            // Extra soft outer glow
            sf::RectangleShape outerGlow;
            outerGlow.setPosition({drawX - 3.0f, drawY - 3.0f});
            outerGlow.setSize({drawW + 6.0f, drawH + 6.0f});
            outerGlow.setFillColor(sf::Color::Transparent);
            outerGlow.setOutlineThickness(3.0f);
            outerGlow.setOutlineColor(sf::Color(255, 255, 200, 50));
            window_.draw(outerGlow);
        }

        drawCenteredText("返回主菜单", sf::FloatRect({drawX, drawY}, {drawW, drawH}),
                         static_cast<unsigned>(25.0f * scale),
                         sf::Color(252, 248, 238, 240), sf::Text::Bold);
    }
}

void Game::drawModeSelectScene() {
    // Background image
    if (menuBgTex_.getSize().x > 0) {
        sf::Sprite bgSpr(menuBgTex_);
        const auto texSize = menuBgTex_.getSize();
        bgSpr.setScale({1280.0f / static_cast<float>(texSize.x),
                        820.0f / static_cast<float>(texSize.y)});
        window_.draw(bgSpr);
    }

    const float modeTime = modeSelectClock_.getElapsedTime().asSeconds();

    // Noise blur overlay — drifting + alpha pulse
    if (noiseTex_.getSize().x > 0) {
        const int driftX = static_cast<int>(modeTime * 12.0f) % 64;
        const int driftY = static_cast<int>(modeTime * 8.0f) % 64;
        const auto noiseAlpha = static_cast<std::uint8_t>(18.0f + std::sin(modeTime * 0.7f) * 6.0f);
        sf::Sprite noiseSpr(noiseTex_);
        noiseSpr.setPosition({0.0f, 0.0f});
        noiseSpr.setTextureRect(sf::IntRect({driftX, driftY}, {1280, 820}));
        noiseSpr.setColor(sf::Color(255, 255, 255, noiseAlpha));
        window_.draw(noiseSpr);
    }

    // Subtle dark vignette — breathing alpha
    {
        const auto vigAlpha = static_cast<std::uint8_t>(60.0f + std::sin(modeTime * 0.5f) * 15.0f);
        sf::RectangleShape overlay;
        overlay.setPosition({0.0f, 0.0f});
        overlay.setSize({1280.0f, 820.0f});
        overlay.setFillColor(sf::Color(10, 8, 18, vigAlpha));
        window_.draw(overlay);
    }

    // Firefly particles
    {
        constexpr int kFireflyCount = 22;
        for (int i = 0; i < kFireflyCount; ++i) {
            const float phase = static_cast<float>(i) * 2.4f;
            const float id = static_cast<float>(i);

            // Erratic wandering motion
            const float driftSpeed = 0.25f + std::fmod(id * 0.07f, 0.15f);
            const float swayAmp = 40.0f + std::fmod(id * 13.0f, 50.0f);
            const float x = 60.0f + std::fmod(
                id * 143.0f + std::sin(modeTime * driftSpeed + phase) * swayAmp
                    + std::cos(modeTime * 0.37f + phase * 1.3f) * 25.0f,
                1160.0f);
            const float y = 820.0f - std::fmod(
                modeTime * (8.0f + std::fmod(id * 3.0f, 10.0f)) + id * 73.0f,
                900.0f);

            // Organic pulsing: bright flash with linger
            const float pulse1 = std::sin(modeTime * 0.9f + phase) * 0.5f + 0.5f;
            const float pulse2 = std::sin(modeTime * 1.7f + phase * 2.1f) * 0.5f + 0.5f;
            const float brightness = pulse1 * 0.65f + pulse2 * 0.35f;
            const float coreAlpha = 0.2f + brightness * 0.8f;
            const float glowAlpha = coreAlpha * 0.35f;

            if (coreAlpha < 0.15f) continue;

            const auto fireflyColor = sf::Color(255, 210, 100,
                                                static_cast<std::uint8_t>(coreAlpha * 220.0f));
            const auto glowColor = sf::Color(255, 200, 80,
                                              static_cast<std::uint8_t>(glowAlpha * 160.0f));

            // Outer glow
            sf::CircleShape glow(7.0f);
            glow.setOrigin({7.0f, 7.0f});
            glow.setPosition({x, y});
            glow.setFillColor(glowColor);
            window_.draw(glow);

            // Bright core
            sf::CircleShape core(2.0f);
            core.setOrigin({2.0f, 2.0f});
            core.setPosition({x, y});
            core.setFillColor(fireflyColor);
            window_.draw(core);
        }
    }

    // Animated title with float + 3D dual-layer
    {
        const float titleFloat = std::sin(modeTime * 1.6f) * 4.0f;
        const float subFloat = std::sin(modeTime * 1.6f + 1.2f) * 2.5f;

        const float titleX = 88.0f;
        const float titleBaseY = 72.0f + titleFloat;
        const float subX = 94.0f;
        const float subBaseY = 144.0f + subFloat;

        // Title: dark offset layer + bright main layer
        drawText("开始游戏", {titleX + 4.0f, titleBaseY - 2.0f}, 54,
                 sf::Color(18, 12, 28, 200), sf::Text::Bold);
        drawText("开始游戏", {titleX, titleBaseY}, 54,
                 sf::Color(255, 242, 210), sf::Text::Bold);

        // Subtitle: subtle shadow + main
        drawText("请在下方选择要进入的对战模式", {subX + 2.0f, subBaseY - 1.0f}, 22,
                 sf::Color(12, 8, 20, 160), sf::Text::Bold);
        drawText("请在下方选择要进入的对战模式", {subX, subBaseY}, 22,
                 sf::Color(220, 205, 175), sf::Text::Bold);
    }

    const sf::Vector2f mousePosition = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));

    // Buttons using 4.png — uniform scale, no stretching
    if (modeBtnTex_.getSize().x > 0) {
        const auto texSize = modeBtnTex_.getSize();
        const float texW = static_cast<float>(texSize.x);
        const float texH = static_cast<float>(texSize.y);

        // Target height 120px, uniform scale
        const float mainScale = 120.0f / texH;
        const float mainW = texW * mainScale;
        const float mainH = texH * mainScale;

        const float colGap = 36.0f;
        const float rowGap = 22.0f;
        const float totalW = mainW * 2.0f + colGap;
        const float leftX = (1280.0f - totalW) * 0.5f;
        const float rightX = leftX + mainW + colGap;
        const float row1Y = 230.0f;
        const float row2Y = row1Y + mainH + rowGap;

        struct ModeBtn {
            sf::Vector2f pos;
            std::string label;
            int animIdx;
        };

        const ModeBtn buttons[] = {
            {{leftX, row1Y}, "经典模式人机对战", 100},
            {{rightX, row1Y}, "幻格模式人机对战", 101},
            {{leftX, row2Y}, "经典模式双人对战", 102},
            {{rightX, row2Y}, "局域网联机对战", 103},
            {{(1280.0f - mainW) * 0.5f, 530.0f}, "返回主菜单", 104},
        };

        for (const auto& btn : buttons) {
            const bool hovered = sf::FloatRect(btn.pos, {mainW, mainH}).contains(mousePosition);
            const bool pressed = buttonAnimIndex_ == btn.animIdx;
            const float scale = pressed ? 0.92f : 1.0f;

            const float drawW = mainW * scale;
            const float drawH = mainH * scale;
            const float drawX = btn.pos.x + (mainW - drawW) * 0.5f;
            const float drawY = btn.pos.y + (mainH - drawH) * 0.5f;
            const float sprScale = mainScale * scale;

            sf::Sprite btnSpr(modeBtnTex_);
            btnSpr.setPosition({drawX, drawY});
            btnSpr.setScale({sprScale, sprScale});
            if (hovered) {
                btnSpr.setColor(sf::Color(255, 255, 235));
            }
            window_.draw(btnSpr);

            // Text follows the scale
            drawCenteredText(btn.label, sf::FloatRect({drawX, drawY}, {drawW, drawH}),
                             static_cast<unsigned>(28.0f * scale),
                             sf::Color(252, 248, 238, 240), sf::Text::Bold);

            // Hover glow
            if (hovered && !pressed) {
                sf::RectangleShape glow;
                glow.setPosition({drawX, drawY});
                glow.setSize({drawW, drawH});
                glow.setFillColor(sf::Color(255, 255, 220, 24));
                window_.draw(glow);
            }
        }
    }
}

void Game::drawDifficultySelectScene() {
    const float diffTime = modeSelectClock_.getElapsedTime().asSeconds();

    // Background image (222.png)
    if (menuBgTex_.getSize().x > 0) {
        sf::Sprite bgSpr(menuBgTex_);
        const auto texSize = menuBgTex_.getSize();
        bgSpr.setScale({1280.0f / static_cast<float>(texSize.x),
                        820.0f / static_cast<float>(texSize.y)});
        window_.draw(bgSpr);
    }

    // Noise blur overlay — drifting + alpha pulse
    if (noiseTex_.getSize().x > 0) {
        const int driftX = static_cast<int>(diffTime * 12.0f) % 64;
        const int driftY = static_cast<int>(diffTime * 8.0f) % 64;
        const auto noiseAlpha = static_cast<std::uint8_t>(18.0f + std::sin(diffTime * 0.7f) * 6.0f);
        sf::Sprite noiseSpr(noiseTex_);
        noiseSpr.setPosition({0.0f, 0.0f});
        noiseSpr.setTextureRect(sf::IntRect({driftX, driftY}, {1280, 820}));
        noiseSpr.setColor(sf::Color(255, 255, 255, noiseAlpha));
        window_.draw(noiseSpr);
    }

    // Vignette breathing
    {
        const auto vigAlpha = static_cast<std::uint8_t>(60.0f + std::sin(diffTime * 0.5f) * 15.0f);
        sf::RectangleShape overlay;
        overlay.setPosition({0.0f, 0.0f});
        overlay.setSize({1280.0f, 820.0f});
        overlay.setFillColor(sf::Color(10, 8, 18, vigAlpha));
        window_.draw(overlay);
    }

    // Fireflies
    {
        constexpr int kFireflyCount = 18;
        for (int i = 0; i < kFireflyCount; ++i) {
            const float phase = static_cast<float>(i) * 2.4f;
            const float id = static_cast<float>(i);

            const float driftSpeed = 0.25f + std::fmod(id * 0.07f, 0.15f);
            const float swayAmp = 40.0f + std::fmod(id * 13.0f, 50.0f);
            const float x = 60.0f + std::fmod(
                id * 143.0f + std::sin(diffTime * driftSpeed + phase) * swayAmp
                    + std::cos(diffTime * 0.37f + phase * 1.3f) * 25.0f,
                1160.0f);
            const float y = 820.0f - std::fmod(
                diffTime * (8.0f + std::fmod(id * 3.0f, 10.0f)) + id * 73.0f,
                900.0f);

            const float pulse1 = std::sin(diffTime * 0.9f + phase) * 0.5f + 0.5f;
            const float pulse2 = std::sin(diffTime * 1.7f + phase * 2.1f) * 0.5f + 0.5f;
            const float brightness = pulse1 * 0.65f + pulse2 * 0.35f;
            const float coreAlpha = 0.2f + brightness * 0.8f;

            if (coreAlpha < 0.15f) continue;

            const auto glowColor = sf::Color(255, 200, 80,
                                              static_cast<std::uint8_t>(coreAlpha * 0.35f * 160.0f));
            const auto coreColor = sf::Color(255, 210, 100,
                                              static_cast<std::uint8_t>(coreAlpha * 220.0f));

            sf::CircleShape glow(7.0f);
            glow.setOrigin({7.0f, 7.0f});
            glow.setPosition({x, y});
            glow.setFillColor(glowColor);
            window_.draw(glow);

            sf::CircleShape core(2.0f);
            core.setOrigin({2.0f, 2.0f});
            core.setPosition({x, y});
            core.setFillColor(coreColor);
            window_.draw(core);
        }
    }

    // Animated title with float + 3D
    {
        const float titleFloat = std::sin(diffTime * 1.6f) * 4.0f;
        const float subFloat = std::sin(diffTime * 1.6f + 1.2f) * 2.5f;

        drawText("选择AI难度", {88.0f + 4.0f, 60.0f + titleFloat - 2.0f}, 54,
                 sf::Color(18, 12, 28, 200), sf::Text::Bold);
        drawText("选择AI难度", {88.0f, 60.0f + titleFloat}, 54,
                 sf::Color(255, 242, 210), sf::Text::Bold);

        const auto subLine = std::string(modeName()) + "人机对战 · 难度选择";
        drawText(subLine, {94.0f + 2.0f, 134.0f + subFloat - 1.0f}, 22,
                 sf::Color(12, 8, 20, 160), sf::Text::Bold);
        drawText(subLine, {94.0f, 134.0f + subFloat}, 22,
                 sf::Color(220, 205, 175), sf::Text::Bold);
    }

    const sf::Vector2f mousePosition = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));

    // Three difficulty cards using 5.png
    constexpr float cardY = 260.0f;
    constexpr float cardWidth = 320.0f;
    constexpr float cardHeight = 280.0f;
    constexpr float cardGap = 30.0f;
    constexpr float totalWidth = cardWidth * 3.0f + cardGap * 2.0f;
    constexpr float startX = (1280.0f - totalWidth) * 0.5f;

    if (diffCardTex_.getSize().x > 0) {
        const auto cardTexSize = diffCardTex_.getSize();
        const float cardTexW = static_cast<float>(cardTexSize.x);
        const float cardTexH = static_cast<float>(cardTexSize.y);

        struct CardDef {
            sf::Vector2f pos;
            int animIdx;
            std::string_view title;
            std::string_view desc1;
            std::string_view desc2;
        };

        const CardDef cards[] = {
            {{startX, cardY}, 200, "简单", "适合入门", "AI偶尔失误"},
            {{startX + cardWidth + cardGap, cardY}, 201, "中等", "势均力敌", "AI攻守兼顾"},
            {{startX + (cardWidth + cardGap) * 2.0f, cardY}, 202, "困难", "巅峰挑战", "AI深度计算"},
        };

        for (const auto& card : cards) {
            const bool hovered = sf::FloatRect(card.pos, {cardWidth, cardHeight}).contains(mousePosition);
            const bool pressed = buttonAnimIndex_ == card.animIdx;
            const float sc = pressed ? 0.92f : 1.0f;

            const float dW = cardWidth * sc;
            const float dH = cardHeight * sc;
            const float dX = card.pos.x + (cardWidth - dW) * 0.5f;
            const float dY = card.pos.y + (cardHeight - dH) * 0.5f;

            sf::Sprite cardSpr(diffCardTex_);
            cardSpr.setPosition({dX, dY});
            cardSpr.setScale({dW / cardTexW, dH / cardTexH});
            if (hovered) {
                cardSpr.setColor(sf::Color(255, 255, 235));
            }
            window_.draw(cardSpr);

            // Hover glow
            if (hovered && !pressed) {
                sf::RectangleShape glow;
                glow.setPosition({dX, dY});
                glow.setSize({dW, dH});
                glow.setFillColor(sf::Color(255, 255, 220, 24));
                window_.draw(glow);
            }

            // Card labels — 3 lines, shadow for readability
            const auto sh = sf::Color(16, 10, 26, 180);
            const auto titleFg = sf::Color(255, 245, 200);
            const auto descFg = sf::Color(228, 205, 158);

            const std::uint32_t titleSz = static_cast<std::uint32_t>(34.0f * sc);
            const std::uint32_t descSz = static_cast<std::uint32_t>(18.0f * sc);
            const float so = 2.0f * sc;

            // Title (shifted down to where English subtitles used to be)
            drawCenteredText(card.title, sf::FloatRect({dX + so, dY + 64.0f * sc + so}, {dW, 52.0f * sc}),
                             titleSz, sh, sf::Text::Bold);
            drawCenteredText(card.title, sf::FloatRect({dX, dY + 64.0f * sc}, {dW, 52.0f * sc}),
                             titleSz, titleFg, sf::Text::Bold);

            // Desc1
            drawCenteredText(card.desc1, sf::FloatRect({dX + so, dY + 136.0f * sc + so}, {dW, 30.0f * sc}),
                             descSz, sh);
            drawCenteredText(card.desc1, sf::FloatRect({dX, dY + 136.0f * sc}, {dW, 30.0f * sc}),
                             descSz, descFg);

            // Desc2
            drawCenteredText(card.desc2, sf::FloatRect({dX + so, dY + 184.0f * sc + so}, {dW, 30.0f * sc}),
                             descSz, sh);
            drawCenteredText(card.desc2, sf::FloatRect({dX, dY + 184.0f * sc}, {dW, 30.0f * sc}),
                             descSz, descFg);
        }
    }

    // Back button using 4.png — uniform scale
    if (modeBtnTex_.getSize().x > 0) {
        const float backScale = 100.0f / static_cast<float>(modeBtnTex_.getSize().y);
        const float backW = static_cast<float>(modeBtnTex_.getSize().x) * backScale;
        const float backH = static_cast<float>(modeBtnTex_.getSize().y) * backScale;
        const float backX = (1280.0f - backW) * 0.5f;
        const float backY = 610.0f;

        const bool backHovered = sf::FloatRect({backX, backY}, {backW, backH}).contains(mousePosition);
        const bool backPressed = buttonAnimIndex_ == 203;
        const float btnScale = backPressed ? 0.92f : 1.0f;

        const float drawW = backW * btnScale;
        const float drawH = backH * btnScale;
        const float drawX = backX + (backW - drawW) * 0.5f;
        const float drawY = backY + (backH - drawH) * 0.5f;

        sf::Sprite btnSpr(modeBtnTex_);
        btnSpr.setPosition({drawX, drawY});
        btnSpr.setScale({backScale * btnScale, backScale * btnScale});
        if (backHovered) {
            btnSpr.setColor(sf::Color(255, 255, 235));
        }
        window_.draw(btnSpr);

        drawCenteredText("返回模式选择", sf::FloatRect({drawX, drawY}, {drawW, drawH}),
                         static_cast<unsigned>(26.0f * btnScale),
                         sf::Color(252, 248, 238, 240), sf::Text::Bold);

        if (backHovered && !backPressed) {
            sf::RectangleShape glow;
            glow.setPosition({drawX, drawY});
            glow.setSize({drawW, drawH});
            glow.setFillColor(sf::Color(255, 255, 220, 24));
            window_.draw(glow);
        }
    }
}

void Game::drawGameScene() {
    // Screen shake during Omen phase
    sf::View savedView = window_.getView();
    if (shakeIntensity_ > 0.0f && (cardEventState_ == CardEventState::Omen || deathAnimPending_ || loversAnimPending_ || worldAnimPending_ || empressAnimPending_ || highPriestessAnimPending_ || sunAnimPending_ || moonAnimPending_ || hermitAnimPending_ || hierophantAnimPending_ || justiceAnimPending_ || hangedManAnimPending_ || devilAnimPending_ || chariotAnimPending_ || magicianAnimPending_ || temperanceAnimPending_ || foolAnimPending_ || towerAnimPending_ || wheelFortuneAnimPending_ || judgementAnimPending_ || (starAnimPending_ && !starInPersistentMode_))) {
        const float shakeElapsed = deathAnimPending_ ? deathAnimClock_.getElapsedTime().asSeconds()
                                   : loversAnimPending_ ? loversAnimClock_.getElapsedTime().asSeconds()
                                   : worldAnimPending_ ? worldAnimClock_.getElapsedTime().asSeconds()
                                   : moonAnimPending_ ? moonAnimClock_.getElapsedTime().asSeconds()
                                   : hermitAnimPending_ ? hermitAnimClock_.getElapsedTime().asSeconds()
                                   : hierophantAnimPending_ ? hierophantAnimClock_.getElapsedTime().asSeconds()
                                   : justiceAnimPending_ ? justiceAnimClock_.getElapsedTime().asSeconds()
                                   : judgementAnimPending_ ? judgementAnimClock_.getElapsedTime().asSeconds()
                                   : wheelFortuneAnimPending_ ? wheelFortuneAnimClock_.getElapsedTime().asSeconds()
                                   : towerAnimPending_ ? towerAnimClock_.getElapsedTime().asSeconds()
                                   : foolAnimPending_ ? foolAnimClock_.getElapsedTime().asSeconds()
                                   : temperanceAnimPending_ ? temperanceAnimClock_.getElapsedTime().asSeconds()
                                   : magicianAnimPending_ ? magicianAnimClock_.getElapsedTime().asSeconds()
                                   : chariotAnimPending_ ? chariotAnimClock_.getElapsedTime().asSeconds()
                                   : devilAnimPending_ ? devilAnimClock_.getElapsedTime().asSeconds()
                                   : hangedManAnimPending_ ? hangedManAnimClock_.getElapsedTime().asSeconds()
                                   : (starAnimPending_ && !starInPersistentMode_) ? starAnimClock_.getElapsedTime().asSeconds()
                                   : cardEventClock_.getElapsedTime().asSeconds();
        const float sx = std::sin(shakeElapsed * 43.0f + 1.7f) * shakeIntensity_;
        const float sy = std::cos(shakeElapsed * 37.0f + 2.3f) * shakeIntensity_;
        sf::View shakeView(sf::FloatRect({sx, sy}, {1280.0f, 820.0f}));
        window_.setView(shakeView);
    }

    // Background image
    if (gameBgTex_.getSize().x > 0) {
        sf::Sprite bgSpr(gameBgTex_);
        const auto texSize = gameBgTex_.getSize();
        bgSpr.setScale({1280.0f / static_cast<float>(texSize.x),
                        820.0f / static_cast<float>(texSize.y)});
        window_.draw(bgSpr);
    }

    // Atmosphere overlay (difficulty-themed)
    const float atmTime = atmosphereClock_.getElapsedTime().asSeconds();
    if (aiDifficulty_ == AIDifficulty::Easy) {
        // Warm glow auras at key positions
        const sf::Vector2f glows[] = {
            {180.0f, 200.0f}, {1100.0f, 180.0f}, {640.0f, 500.0f}
        };
        for (const auto& gpos : glows) {
            sf::CircleShape glow;
            glow.setPosition({gpos.x - 180.0f, gpos.y - 180.0f});
            glow.setRadius(180.0f);
            const float flicker = std::sin(atmTime * 0.7f + gpos.x * 0.01f) * 0.3f + 0.7f;
            glow.setFillColor(sf::Color(255, 235, 200, static_cast<std::uint8_t>(12.0f * flicker)));
            window_.draw(glow);
            sf::CircleShape inner;
            inner.setPosition({gpos.x - 90.0f, gpos.y - 90.0f});
            inner.setRadius(90.0f);
            inner.setFillColor(sf::Color(255, 245, 220, static_cast<std::uint8_t>(8.0f * flicker)));
            window_.draw(inner);
        }
    } else if (aiDifficulty_ == AIDifficulty::Medium) {
        // Breathing candlelight/lantern flicker
        const float baseBreath = std::sin(atmTime * 1.3f) * 0.5f + 0.5f;
        const float microFlicker = std::sin(atmTime * 5.7f) * std::sin(atmTime * 3.1f) * 0.3f;
        const float breath = std::max(0.0f, std::min(1.0f, baseBreath * 0.7f + microFlicker * 0.3f));
        sf::RectangleShape breathOverlay;
        breathOverlay.setPosition({0.0f, 0.0f});
        breathOverlay.setSize({1280.0f, 820.0f});
        breathOverlay.setFillColor(sf::Color(255, 200, 120, static_cast<std::uint8_t>(6.0f + breath * 16.0f)));
        window_.draw(breathOverlay);
    } else if (aiDifficulty_ == AIDifficulty::Hard) {
        // Shifting fog using noise texture
        if (noiseTex_.getSize().x > 0) {
            sf::Sprite fog(noiseTex_);
            fog.setTextureRect(sf::IntRect(
                {static_cast<int>(atmTime * 18.0f) % 1024,
                 static_cast<int>(atmTime * 7.0f) % 1024},
                {1280 + 128, 820 + 128}));
            fog.setPosition({-64.0f, -64.0f});
            fog.setScale({1.1f, 1.1f});
            fog.setColor(sf::Color(20, 12, 8, 18));
            window_.draw(fog);
        }
    }

    // Ambient particles (difficulty-themed)
    for (const auto& p : gameParticles_) {
        const float lifeRatio = p.life / p.maxLife;
        const std::uint8_t alpha = static_cast<std::uint8_t>(lifeRatio * 255.0f);
        if (aiDifficulty_ == AIDifficulty::Medium) {
            // Firefly: outer glow + bright core
            const float pulse = std::sin(p.alphaPhase + gameParticleClock_.getElapsedTime().asSeconds() * 3.0f) * 0.3f + 0.7f;
            sf::CircleShape glow;
            glow.setPosition({p.pos.x - p.size * 2.0f, p.pos.y - p.size * 2.0f});
            glow.setRadius(p.size * 2.0f);
            glow.setFillColor(sf::Color(p.color.r, p.color.g, p.color.b, static_cast<std::uint8_t>(alpha * 0.15f * pulse)));
            window_.draw(glow);
            sf::CircleShape core;
            core.setPosition({p.pos.x - p.size * 0.5f, p.pos.y - p.size * 0.5f});
            core.setRadius(p.size * 0.5f);
            core.setFillColor(sf::Color(p.color.r, p.color.g, p.color.b, static_cast<std::uint8_t>(alpha * pulse)));
            window_.draw(core);
        } else {
            // Petals and embers: small rectangles
            sf::RectangleShape rect;
            rect.setPosition(p.pos);
            rect.setSize({p.size, p.size});
            rect.setFillColor(sf::Color(p.color.r, p.color.g, p.color.b, alpha));
            if (aiDifficulty_ == AIDifficulty::Hard) {
                // Embers: slight rotation-like offset (jagged feel via positioning)
                rect.setSize({p.size, p.size * 1.3f});
            }
            window_.draw(rect);
        }
    }

    // 2.5D raised frosted glass platform
    {
        constexpr float cardX = 97.0f;
        constexpr float cardY = 162.0f;
        constexpr float cardW = 570.0f;
        constexpr float cardH = 570.0f;
        constexpr float bevel = 4.0f;
        constexpr float shadowOff = 5.0f;

        // Drop shadow
        {
            sf::RectangleShape shadow;
            shadow.setPosition({cardX + shadowOff, cardY + shadowOff});
            shadow.setSize({cardW, cardH});
            shadow.setFillColor(sf::Color(20, 16, 28, 100));
            window_.draw(shadow);

            if (noiseTex_.getSize().x > 0) {
                sf::Sprite noiseSpr(noiseTex_);
                noiseSpr.setPosition({cardX + shadowOff, cardY + shadowOff});
                noiseSpr.setTextureRect(sf::IntRect({0, 0}, {static_cast<int>(cardW), static_cast<int>(cardH)}));
                noiseSpr.setColor(sf::Color(0, 0, 0, 12));
                window_.draw(noiseSpr);
            }
        }

        // Glass body
        {
            sf::RectangleShape body;
            body.setPosition({cardX, cardY});
            body.setSize({cardW, cardH});
            body.setFillColor(sf::Color(235, 225, 208, 90));
            window_.draw(body);

            if (noiseTex_.getSize().x > 0) {
                sf::Sprite noiseSpr(noiseTex_);
                noiseSpr.setPosition({cardX, cardY});
                noiseSpr.setTextureRect(sf::IntRect({0, 0}, {static_cast<int>(cardW), static_cast<int>(cardH)}));
                noiseSpr.setColor(sf::Color(255, 255, 255, 18));
                window_.draw(noiseSpr);
            }
        }

        // Top bevel — bright highlight
        {
            sf::RectangleShape top;
            top.setPosition({cardX, cardY});
            top.setSize({cardW, bevel});
            top.setFillColor(sf::Color(255, 250, 240, 160));
            window_.draw(top);
        }

        // Left bevel — bright highlight
        {
            sf::RectangleShape left;
            left.setPosition({cardX, cardY});
            left.setSize({bevel, cardH});
            left.setFillColor(sf::Color(255, 250, 240, 140));
            window_.draw(left);
        }

        // Bottom bevel — dark shadow
        {
            sf::RectangleShape bottom;
            bottom.setPosition({cardX, cardY + cardH - bevel});
            bottom.setSize({cardW, bevel});
            bottom.setFillColor(sf::Color(80, 60, 40, 120));
            window_.draw(bottom);
        }

        // Right bevel — dark shadow
        {
            sf::RectangleShape right;
            right.setPosition({cardX + cardW - bevel, cardY});
            right.setSize({bevel, cardH});
            right.setFillColor(sf::Color(80, 60, 40, 100));
            window_.draw(right);
        }

        // Top-left corner pixel — extra bright
        {
            sf::RectangleShape corner;
            corner.setPosition({cardX, cardY});
            corner.setSize({bevel, bevel});
            corner.setFillColor(sf::Color(255, 252, 245, 200));
            window_.draw(corner);
        }
    }

    // 2.5D frosted glass side panel
    {
        constexpr float panelX = 772.0f;
        constexpr float panelY = 255.0f;
        constexpr float panelW = 468.0f;
        constexpr float panelH = 520.0f;
        constexpr float bevel = 4.0f;
        constexpr float shadowOff = 5.0f;

        // Drop shadow
        {
            sf::RectangleShape shadow;
            shadow.setPosition({panelX + shadowOff, panelY + shadowOff});
            shadow.setSize({panelW, panelH});
            shadow.setFillColor(sf::Color(20, 16, 28, 100));
            window_.draw(shadow);

            if (noiseTex_.getSize().x > 0) {
                sf::Sprite noiseSpr(noiseTex_);
                noiseSpr.setPosition({panelX + shadowOff, panelY + shadowOff});
                noiseSpr.setTextureRect(sf::IntRect({0, 0}, {static_cast<int>(panelW), static_cast<int>(panelH)}));
                noiseSpr.setColor(sf::Color(0, 0, 0, 12));
                window_.draw(noiseSpr);
            }
        }

        // Glass body
        {
            sf::RectangleShape body;
            body.setPosition({panelX, panelY});
            body.setSize({panelW, panelH});
            body.setFillColor(sf::Color(34, 30, 40, 130));
            window_.draw(body);

            if (noiseTex_.getSize().x > 0) {
                sf::Sprite noiseSpr(noiseTex_);
                noiseSpr.setPosition({panelX, panelY});
                noiseSpr.setTextureRect(sf::IntRect({0, 0}, {static_cast<int>(panelW), static_cast<int>(panelH)}));
                noiseSpr.setColor(sf::Color(255, 255, 255, 12));
                window_.draw(noiseSpr);
            }
        }

        // Top bevel
        {
            sf::RectangleShape top;
            top.setPosition({panelX, panelY});
            top.setSize({panelW, bevel});
            top.setFillColor(sf::Color(160, 150, 180, 120));
            window_.draw(top);
        }

        // Left bevel
        {
            sf::RectangleShape left;
            left.setPosition({panelX, panelY});
            left.setSize({bevel, panelH});
            left.setFillColor(sf::Color(160, 150, 180, 100));
            window_.draw(left);
        }

        // Bottom bevel
        {
            sf::RectangleShape bottom;
            bottom.setPosition({panelX, panelY + panelH - bevel});
            bottom.setSize({panelW, bevel});
            bottom.setFillColor(sf::Color(20, 16, 28, 100));
            window_.draw(bottom);
        }

        // Right bevel
        {
            sf::RectangleShape right;
            right.setPosition({panelX + panelW - bevel, panelY});
            right.setSize({bevel, panelH});
            right.setFillColor(sf::Color(20, 16, 28, 80));
            window_.draw(right);
        }

        // Top-left corner
        {
            sf::RectangleShape corner;
            corner.setPosition({panelX, panelY});
            corner.setSize({bevel, bevel});
            corner.setFillColor(sf::Color(190, 180, 210, 170));
            window_.draw(corner);
        }
    }

    board_.draw(window_);

    // Temperance persistent obstacle glow (while effect active)
    if (temperanceRemaining_ > 0 && temperanceMarkAlpha_ > 0.005f) {
        const float cellHalf = board_.cellToPixel(0, 1).x - board_.cellToPixel(0, 0).x;
        const auto ga = static_cast<std::uint8_t>(temperanceMarkAlpha_ * 80.0f);
        for (const auto& op : board_.obstaclePositions()) {
            const auto opx = board_.cellToPixel(op.x, op.y);
            const float glowR = cellHalf * 0.55f;
            sf::CircleShape glow(glowR);
            glow.setOrigin({glowR, glowR});
            glow.setPosition(opx);
            glow.setFillColor(sf::Color(100, 160, 220, ga));
            window_.draw(glow);
        }
    }

    // Obstacle removal animations
    for (const auto& anim : obstacleRemovalAnims_) {
        const float px = anim.pixelPos.x;
        const float py = anim.pixelPos.y;
        const float obsHalf = 16.0f; // obstacle texture is 32x32

        if (anim.emperorStyle) {
            // ── Emperor: Crown fade-in → Particle surge → Crown + obstacle dissolve (1.2s) ──
            const float prog = anim.elapsed / 1.2f;
            const bool hasCrown = crownTex_.getSize().x > 0;
            const float crownW = hasCrown ? 84.0f : 49.5f;  // target display width
            const float crownH = hasCrown ? crownW * static_cast<float>(crownTex_.getSize().y)
                                               / static_cast<float>(crownTex_.getSize().x)
                                          : 36.0f;
            const float crownScale = hasCrown ? crownW / static_cast<float>(crownTex_.getSize().x) : 1.0f;
            const float crownCx = px;
            const float crownCy = py - obsHalf - crownH * 0.75f;  // small gap above obstacle

            auto drawCrownSprite = [&](float alpha, int clipFromTopRows, int totalClipRows) {
                if (alpha <= 0.005f || !hasCrown) return;
                const auto ca = static_cast<std::uint8_t>(alpha * 255.0f);
                const auto texW = static_cast<int>(crownTex_.getSize().x);
                const auto texH = static_cast<int>(crownTex_.getSize().y);
                const int clipPx = texH * clipFromTopRows / totalClipRows;
                const int visibleH = texH - clipPx;
                if (visibleH <= 0) return;

                sf::Sprite crownSpr(crownTex_);
                crownSpr.setTextureRect(sf::IntRect({0, clipPx}, {texW, visibleH}));
                crownSpr.setScale({crownScale, crownScale});
                crownSpr.setPosition({std::round(crownCx - crownW * 0.5f),
                                      std::round(crownCy + clipPx * crownScale)});
                crownSpr.setColor(sf::Color(255, 255, 255, ca));
                window_.draw(crownSpr);
            };

            // ─── Phase 1: Crown fades in (0–0.35s) ───
            const float appearProg = std::min(1.0f, prog / 0.292f);
            if (appearProg < 1.0f) {
                drawCrownSprite(appearProg, 0, 1);
            }

            // ─── Phase 2: Crown holds + particles surge from below (0.292–0.71 normalized) ───
            const float particleProg = (prog - 0.292f) / 0.418f;
            if (particleProg > 0.0f && particleProg < 1.2f) {
                const float crownHoldAlpha = particleProg < 1.0f ? 1.0f : 1.0f - (particleProg - 1.0f);
                drawCrownSprite(crownHoldAlpha, 0, 1);

                // Golden particles erupting from beneath obstacle
                const float eruption = std::min(1.0f, particleProg / 0.12f);
                const int numStreams = 5;
                for (int s = 0; s < numStreams; ++s) {
                    const float sx = px - obsHalf + obsHalf * 2.0f * (static_cast<float>(s) + 0.5f) / numStreams;
                    const int particlesPerStream = 5;
                    for (int p = 0; p < particlesPerStream; ++p) {
                        const float pPhase = static_cast<float>(p) / particlesPerStream;
                        const float pLife = std::max(0.0f, (particleProg - pPhase * 0.25f) * eruption);
                        if (pLife <= 0.0f || pLife >= 1.0f) continue;

                        const float riseY = pLife * obsHalf * 3.5f;
                        const float swayX = std::sin(pLife * 5.5f + s * 1.3f + p * 0.7f) * obsHalf * 0.7f;
                        const auto pa = static_cast<std::uint8_t>((1.0f - pLife) * 230.0f);
                        const float pSize = 2.5f + (1.0f - pLife) * 3.5f;

                        sf::RectangleShape particle;
                        particle.setPosition({std::round(sx + swayX - pSize * 0.5f),
                                              std::round(py + obsHalf - riseY - pSize * 0.5f)});
                        particle.setSize({std::round(pSize), std::round(pSize)});
                        const auto brightness = static_cast<std::uint8_t>(210.0f + (1.0f - pLife) * 45.0f);
                        particle.setFillColor(sf::Color(brightness, static_cast<std::uint8_t>(brightness * 0.8f), 18, pa));
                        window_.draw(particle);
                    }
                }

                // Faint golden glow rising from beneath obstacle
                const float glowRise = eruption * obsHalf * 2.5f;
                if (glowRise > 0.5f) {
                    sf::RectangleShape underGlow;
                    underGlow.setPosition({std::round(px - obsHalf - 4.0f),
                                           std::round(py + obsHalf - glowRise)});
                    underGlow.setSize({obsHalf * 2.0f + 8.0f, glowRise});
                    underGlow.setFillColor(sf::Color(255, 180, 20,
                        static_cast<std::uint8_t>(eruption * 100.0f)));
                    window_.draw(underGlow);
                }
            }

            // ─── Phase 3: Crown + obstacle dissolve together row-by-row (0.71–1.0) ───
            const float dissolveProg = (prog - 0.71f) / 0.29f;
            if (dissolveProg > 0.0f && dissolveProg < 1.0f) {
                const int totalRows = 10;
                const float totalHeight = obsHalf * 2.0f + crownH;
                const float rowH = totalHeight / static_cast<float>(totalRows);
                const int dissolvedRows = static_cast<int>(dissolveProg * totalRows);

                // Obstacle: clip from top
                const float obsStartY = py - obsHalf;
                const float obsEndY = py + obsHalf;
                const float obsVisibleTop = obsStartY + dissolvedRows * rowH;
                if (obsVisibleTop < obsEndY) {
                    sf::RectangleShape remaining;
                    remaining.setPosition({std::round(px - obsHalf), std::round(obsVisibleTop)});
                    remaining.setSize({obsHalf * 2.0f, obsEndY - obsVisibleTop});
                    remaining.setFillColor(sf::Color(60, 20, 60, 180));
                    window_.draw(remaining);
                }

                // Crown: clip from top (proportional to dissolve)
                const int crownTotalRows = totalRows;
                const int crownDissolved = static_cast<int>(dissolveProg * crownTotalRows);
                drawCrownSprite(1.0f - dissolveProg * 0.8f, crownDissolved, crownTotalRows);

                // Particles rising from dissolved area
                for (int r = 0; r < dissolvedRows; ++r) {
                    const float rowLife = (dissolveProg - static_cast<float>(r) / totalRows) / (1.0f / totalRows);
                    if (rowLife <= 0.0f || rowLife >= 1.0f) continue;

                    const float obsRowY = py - obsHalf + (r + 0.5f) * rowH;
                    const int numP = 4;
                    for (int p = 0; p < numP; ++p) {
                        const float pxOff = (static_cast<float>(p) / numP - 0.4f) * obsHalf * 1.8f;
                        const float riseY = rowLife * rowH * 3.0f;
                        const float sway = std::sin(rowLife * 7.5f + p * 2.3f) * obsHalf * 0.55f;
                        const auto pa = static_cast<std::uint8_t>((1.0f - rowLife) * 210.0f);
                        const float pSize = 2.0f + (1.0f - rowLife) * 2.5f;

                        sf::RectangleShape pDot;
                        pDot.setPosition({std::round(px + pxOff + sway - pSize * 0.5f),
                                          std::round(obsRowY - riseY - pSize * 0.5f)});
                        pDot.setSize({std::round(pSize), std::round(pSize)});
                        pDot.setFillColor(sf::Color(255, 200, 25, pa));
                        window_.draw(pDot);
                    }
                }
            }

            // Fallback: procedural crown if texture not loaded
            if (!hasCrown) {
                constexpr float cp = 4.5f;
                constexpr int cw = 11, ch = 8;
                static constexpr bool kPat[ch][cw] = {
                    {0,0,0,0,1,0,0,0,0,0,0}, {0,0,0,1,0,1,0,0,0,0,0},
                    {0,0,1,0,0,0,1,0,0,0,0}, {1,0,0,0,0,0,0,1,0,0,0},
                    {1,0,0,1,0,0,1,0,0,0,0}, {1,0,0,0,0,0,0,1,0,0,0},
                    {1,1,1,1,1,1,1,1,1,1,1}, {0,1,1,1,1,1,1,1,1,1,0},
                };
                float alpha = 1.0f;
                int clipRows = 0;
                if (appearProg < 1.0f) { alpha = appearProg; }
                else if (particleProg > 0.0f && particleProg < 1.2f) { alpha = particleProg < 1.0f ? 1.0f : 1.0f - (particleProg - 1.0f); }
                else if (dissolveProg > 0.0f && dissolveProg < 1.0f) {
                    alpha = 1.0f - dissolveProg * 0.8f;
                    clipRows = static_cast<int>(dissolveProg * ch);
                } else if (prog >= 1.0f) { alpha = 0.0f; }
                if (alpha > 0.005f) {
                    const auto ca = static_cast<std::uint8_t>(alpha * 255.0f);
                    const float fcy = py - obsHalf - cp * static_cast<float>(ch) + 2.0f;
                    for (int gy = clipRows; gy < ch; ++gy) {
                        for (int gx = 0; gx < cw; ++gx) {
                            if (!kPat[gy][gx]) continue;
                            sf::RectangleShape dot;
                            dot.setPosition({std::round(px + (gx - cw/2) * cp), std::round(fcy + gy * cp)});
                            dot.setSize({std::round(cp), std::round(cp)});
                            const bool isPoint = (gy <= 2);
                            dot.setFillColor(sf::Color(
                                static_cast<std::uint8_t>(isPoint ? 255 : 240),
                                static_cast<std::uint8_t>(isPoint ? 245 : 200),
                                static_cast<std::uint8_t>(isPoint ? 200 : 30), ca));
                            window_.draw(dot);
                        }
                    }
                }
            }
        } else {
            // ── Generic: pixel-block scatter dissolve (0.35s) ──
            const float prog = anim.elapsed / 0.35f;
            const int grid = 4;
            const float blockSize = obsHalf * 2.0f / static_cast<float>(grid);

            for (int by = 0; by < grid; ++by) {
                for (int bx = 0; bx < grid; ++bx) {
                    const float bxOff = (static_cast<float>(bx) / grid - 0.5f + 0.125f);
                    const float byOff = (static_cast<float>(by) / grid - 0.5f + 0.125f);
                    const float scatter = prog * 1.8f;
                    const float dist = std::sqrt(bxOff * bxOff + byOff * byOff) + 0.1f;
                    const float angle = std::atan2(byOff, bxOff);
                    const float blockCx = px + bxOff * obsHalf * 2.0f + std::cos(angle) * scatter * obsHalf * dist;
                    const float blockCy = py + byOff * obsHalf * 2.0f + std::sin(angle) * scatter * obsHalf * dist;
                    const float rotAngle = prog * 3.0f * (bx + by) * 0.3f;
                    const auto ba = static_cast<std::uint8_t>((1.0f - prog * prog) * 200.0f);

                    sf::RectangleShape block;
                    block.setPosition({std::round(blockCx - blockSize * 0.45f),
                                       std::round(blockCy - blockSize * 0.45f)});
                    block.setSize({std::round(blockSize * 0.9f * (1.0f - prog * 0.5f)),
                                   std::round(blockSize * 0.9f * (1.0f - prog * 0.5f))});
                    block.setRotation(sf::degrees(rotAngle * 57.29578f));
                    block.setFillColor(sf::Color(80, 30, 80, ba));
                    window_.draw(block);
                }
            }
        }
    }

    // Star card animation rendering (enhanced guiding star — private: only drawer sees it)
    if (starHighlightValid_ && (!networkMode_ || iAmPicker_)) {
        const auto px = board_.cellToPixel(starHighlightPos_.x, starHighlightPos_.y);
        const float starY = px.y + starYOffset_;

        // --- Ground glow ring (landing zone indicator) ---
        if (starAnimPending_) {
            const float ringAlpha = starInPersistentMode_
                ? 0.35f + 0.15f * std::sin(starAnimClock_.getElapsedTime().asSeconds() * 1.8f)
                : starAlpha_ * 0.5f;
            const auto ra = static_cast<std::uint8_t>(ringAlpha * 220.0f);
            const float ringR = starInPersistentMode_ ? 14.0f : 10.0f + starScale_ * 6.0f;
            sf::CircleShape ring(ringR, 20);
            ring.setOrigin({ringR, ringR});
            ring.setPosition(px);
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineThickness(1.5f);
            ring.setOutlineColor(sf::Color(180, 210, 255, ra));
            window_.draw(ring);
            // Inner glow
            sf::CircleShape inGlow(ringR - 2.0f, 20);
            inGlow.setOrigin({ringR - 2.0f, ringR - 2.0f});
            inGlow.setPosition(px);
            inGlow.setFillColor(sf::Color(160, 200, 255, static_cast<std::uint8_t>(ra * 0.3f)));
            window_.draw(inGlow);
        }

        // --- 8 directional rays (Phase 1 bloom only) ---
        if (starRayAlpha_ > 0.01f) {
            const auto ra = static_cast<std::uint8_t>(starRayAlpha_ * 200.0f);
            constexpr int kRays = 8;
            for (int i = 0; i < kRays; ++i) {
                const float angle = static_cast<float>(i) / static_cast<float>(kRays) * 2.0f * 3.14159265f;
                const float rayLen = 20.0f + starRayAlpha_ * 10.0f;
                const float rx = px.x + std::cos(angle) * 8.0f;
                const float ry = starY + std::sin(angle) * 8.0f;
                const float ex = px.x + std::cos(angle) * rayLen;
                const float ey = starY + std::sin(angle) * rayLen;
                sf::VertexArray rayLine(sf::PrimitiveType::Lines, 2);
                rayLine[0].position = {rx, ry};
                rayLine[0].color = sf::Color(200, 220, 255, ra);
                rayLine[1].position = {ex, ey};
                rayLine[1].color = sf::Color(180, 200, 255, static_cast<std::uint8_t>(ra * 0.3f));
                window_.draw(rayLine);
            }
        }

        // --- Star sprite ---
        if (starSpriteTex_.getSize().x > 0 && starAlpha_ > 0.01f) {
            const auto ssz = sf::Vector2f(starSpriteTex_.getSize());
            // Outer glow
            sf::Sprite starGlow(starSpriteTex_);
            starGlow.setOrigin({ssz.x * 0.5f, ssz.y * 0.5f});
            starGlow.setPosition({px.x, starY});
            starGlow.setScale({starScale_ * 1.8f, starScale_ * 1.8f});
            starGlow.setRotation(sf::degrees(starRotation_));
            starGlow.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(starAlpha_ * 60.0f)));
            window_.draw(starGlow);
            // Main star
            sf::Sprite starSpr(starSpriteTex_);
            starSpr.setOrigin({ssz.x * 0.5f, ssz.y * 0.5f});
            starSpr.setPosition({px.x, starY});
            starSpr.setScale({starScale_, starScale_});
            starSpr.setRotation(sf::degrees(starRotation_));
            starSpr.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(starAlpha_ * 230.0f)));
            window_.draw(starSpr);
        }

        // --- "希望之星" floating text (Phase 1) ---
        if (!starInPersistentMode_ && starRayAlpha_ > 0.35f && fontLoaded_) {
            const float textAlpha = starRayAlpha_ * 0.6f;
            const float textY = starY - 28.0f;
            const std::string stStr = "希望之星";
            const auto stUtf8 = sf::String::fromUtf8(stStr.begin(), stStr.end());
            sf::Text stShadow(uiFont_, stUtf8, 11);
            stShadow.setScale({2.0f, 2.0f});
            stShadow.setFillColor(sf::Color(15, 25, 50, static_cast<std::uint8_t>(textAlpha * 140.0f)));
            auto sb = stShadow.getLocalBounds();
            stShadow.setOrigin({sb.size.x * 0.5f, sb.size.y * 0.5f});
            stShadow.setPosition({px.x + 2.0f, textY + 2.0f});
            window_.draw(stShadow);
            sf::Text stMain(uiFont_, stUtf8, 11);
            stMain.setScale({2.0f, 2.0f});
            stMain.setFillColor(sf::Color(170, 210, 255, static_cast<std::uint8_t>(textAlpha * 255.0f)));
            stMain.setOrigin({sb.size.x * 0.5f, sb.size.y * 0.5f});
            stMain.setPosition({px.x, textY});
            window_.draw(stMain);
            sf::Text stHi(uiFont_, stUtf8, 11);
            stHi.setScale({2.0f, 2.0f});
            stHi.setFillColor(sf::Color(220, 240, 255, static_cast<std::uint8_t>(textAlpha * 120.0f)));
            stHi.setOrigin({sb.size.x * 0.5f, sb.size.y * 0.5f});
            stHi.setPosition({px.x - 1.0f, textY - 1.0f});
            window_.draw(stHi);
        }

        // --- Stardust particles ---
        if (!starDustParticles_.empty() && starDustTex_.getSize().x > 0) {
            const auto dsz = sf::Vector2f(starDustTex_.getSize());
            for (const auto& sd : starDustParticles_) {
                const float lifeRatio = 1.0f - sd.life / sd.maxLife;
                const auto alpha = static_cast<std::uint8_t>(lifeRatio * 190.0f);
                if (alpha == 0) continue;
                sf::Sprite dust(starDustTex_);
                dust.setOrigin({dsz.x * 0.5f, dsz.y * 0.5f});
                dust.setPosition(sd.pos);
                dust.setScale({sd.scale * lifeRatio, sd.scale * lifeRatio});
                dust.setRotation(sf::degrees(sd.rotation));
                dust.setColor(sf::Color(255, 255, 255, alpha));
                window_.draw(dust);
            }
        }
    }

    // Moon card animation rendering (Lunar Mist)
    if (moonAnimPending_ && moonTex_.getSize().x > 0) {
        const float mElapsed = moonAnimClock_.getElapsedTime().asSeconds();

        // Silver-blue overlay tint on board area
        if (moonOverlayAlpha_ > 0.005f) {
            const auto boardLeft = board_.cellToPixel(0, 0).x - 12.0f;
            const auto boardTop = board_.cellToPixel(0, 0).y - 12.0f;
            const auto boardRight = board_.cellToPixel(0, Board::kBoardSize - 1).x + 12.0f;
            const auto boardBottom = board_.cellToPixel(Board::kBoardSize - 1, 0).y + 12.0f;
            sf::RectangleShape overlay({boardRight - boardLeft, boardBottom - boardTop});
            overlay.setPosition({boardLeft, boardTop});
            overlay.setFillColor(sf::Color(180, 200, 230,
                static_cast<std::uint8_t>(moonOverlayAlpha_ * 38.0f)));
            window_.draw(overlay);
        }

        // Mist particles
        if (!moonMistParticles_.empty() && moonMistTex_.getSize().x > 0) {
            const auto msz = sf::Vector2f(moonMistTex_.getSize());
            for (const auto& mp : moonMistParticles_) {
                const float lifeRatio = 1.0f - mp.life / mp.maxLife;
                const float fadeAlpha = (mElapsed > 0.9f) ? (1.0f - (mElapsed - 0.9f) / 0.4f) : 1.0f;
                const auto alpha = static_cast<std::uint8_t>(lifeRatio * fadeAlpha * moonMistAlpha_ * 120.0f);
                if (alpha == 0) continue;
                sf::Sprite mist(moonMistTex_);
                mist.setOrigin({msz.x * 0.5f, msz.y * 0.5f});
                mist.setPosition(mp.pos);
                mist.setScale({mp.scale * lifeRatio, mp.scale * (0.6f + 0.4f * lifeRatio)});
                mist.setColor(sf::Color(200, 215, 240, alpha));
                window_.draw(mist);
            }
        }

        // Crescent moon sprite
        const auto msz = sf::Vector2f(moonTex_.getSize());
        const sf::Vector2f moonPos(board_.cellToPixel(7, 7).x + moonYOffset_ + 60.0f,
                                     board_.cellToPixel(0, 7).y - 55.0f + moonYOffset_);
        // Moon glow
        {
            sf::CircleShape glow(38.0f);
            glow.setOrigin({38.0f, 38.0f});
            glow.setPosition(moonPos);
            glow.setFillColor(sf::Color(180, 200, 240,
                static_cast<std::uint8_t>(moonAlpha_ * 60.0f)));
            window_.draw(glow);
        }
        // Moon sprite
        {
            sf::Sprite moon(moonTex_);
            moon.setOrigin({msz.x * 0.5f, msz.y * 0.5f});
            moon.setPosition(moonPos);
            moon.setScale({2.0f, 2.0f});
            moon.setColor(sf::Color(255, 255, 255,
                static_cast<std::uint8_t>(moonAlpha_ * 230.0f)));
            window_.draw(moon);
        }
        // Inner bright glow
        {
            sf::CircleShape innerGlow(18.0f);
            innerGlow.setOrigin({18.0f, 18.0f});
            innerGlow.setPosition(moonPos);
            innerGlow.setFillColor(sf::Color(220, 235, 255,
                static_cast<std::uint8_t>(moonAlpha_ * 80.0f)));
            window_.draw(innerGlow);
        }
    }

    // Moon original-position ghost marker — where AI wanted to place before mist offset
    if (moonMarkerAlpha_ > 0.005f && moonOriginalPos_.x >= 0) {
        const auto cellPx = board_.cellToPixel(moonOriginalPos_.x, moonOriginalPos_.y);
        const float cellR = (board_.cellToPixel(0, 1).x - board_.cellToPixel(0, 0).x) * 0.42f;
        const auto ma = static_cast<std::uint8_t>(moonMarkerAlpha_ * 160.0f);
        // Outer ring (dashed feel with thin stroke)
        sf::CircleShape ring(cellR);
        ring.setOrigin({cellR, cellR});
        ring.setPosition(cellPx);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineColor(sf::Color(190, 210, 240, ma));
        ring.setOutlineThickness(2.0f);
        window_.draw(ring);
        // Inner smaller ring
        sf::CircleShape innerRing(cellR * 0.55f);
        innerRing.setOrigin({cellR * 0.55f, cellR * 0.55f});
        innerRing.setPosition(cellPx);
        innerRing.setFillColor(sf::Color::Transparent);
        innerRing.setOutlineColor(sf::Color(200, 220, 250, static_cast<std::uint8_t>(ma * 0.7f)));
        innerRing.setOutlineThickness(1.5f);
        window_.draw(innerRing);
        // Crosshair lines
        sf::VertexArray crossH(sf::PrimitiveType::Lines, 4);
        crossH[0].position = {cellPx.x - cellR * 1.15f, cellPx.y};
        crossH[1].position = {cellPx.x + cellR * 1.15f, cellPx.y};
        crossH[2].position = {cellPx.x, cellPx.y - cellR * 1.15f};
        crossH[3].position = {cellPx.x, cellPx.y + cellR * 1.15f};
        crossH[0].color = sf::Color(210, 225, 250, static_cast<std::uint8_t>(ma * 0.6f));
        crossH[1].color = sf::Color(210, 225, 250, static_cast<std::uint8_t>(ma * 0.6f));
        crossH[2].color = sf::Color(210, 225, 250, static_cast<std::uint8_t>(ma * 0.6f));
        crossH[3].color = sf::Color(210, 225, 250, static_cast<std::uint8_t>(ma * 0.6f));
        window_.draw(crossH);
        // Glow dot at center
        sf::CircleShape dot(4.0f);
        dot.setOrigin({4.0f, 4.0f});
        dot.setPosition(cellPx);
        dot.setFillColor(sf::Color(230, 240, 255, ma));
        window_.draw(dot);
    }

    // Hermit card animation rendering (Mirror-Still Water)
    if (hermitAnimPending_ && hermitPondTex_.getSize().x > 0) {
        const sf::Vector2f pondCenter = board_.cellToPixel(7, 7);

        // Deep indigo overlay on board area
        if (hermitOverlayAlpha_ > 0.005f) {
            const auto boardLeft = board_.cellToPixel(0, 0).x - 12.0f;
            const auto boardTop = board_.cellToPixel(0, 0).y - 12.0f;
            const auto boardRight = board_.cellToPixel(0, Board::kBoardSize - 1).x + 12.0f;
            const auto boardBottom = board_.cellToPixel(Board::kBoardSize - 1, 0).y + 12.0f;
            sf::RectangleShape overlay({boardRight - boardLeft, boardBottom - boardTop});
            overlay.setPosition({boardLeft, boardTop});
            overlay.setFillColor(sf::Color(30, 42, 72,
                static_cast<std::uint8_t>(hermitOverlayAlpha_ * 255.0f)));
            window_.draw(overlay);
        }

        // Pond circle
        if (hermitPondAlpha_ > 0.005f) {
            const auto pa = static_cast<std::uint8_t>(hermitPondAlpha_ * 180.0f);
            const auto psz = sf::Vector2f(hermitPondTex_.getSize());
            sf::Sprite pond(hermitPondTex_);
            pond.setOrigin({psz.x * 0.5f, psz.y * 0.5f});
            pond.setPosition(pondCenter);
            const float pondScale = hermitPondRadius_ * 2.0f / psz.x;
            pond.setScale({pondScale, pondScale});
            pond.setColor(sf::Color(255, 255, 255, pa));
            window_.draw(pond);

            // Pond edge ring
            sf::CircleShape edgeRing(hermitPondRadius_);
            edgeRing.setOrigin({hermitPondRadius_, hermitPondRadius_});
            edgeRing.setPosition(pondCenter);
            edgeRing.setFillColor(sf::Color::Transparent);
            edgeRing.setOutlineColor(sf::Color(100, 140, 200, static_cast<std::uint8_t>(pa * 0.5f)));
            edgeRing.setOutlineThickness(2.5f);
            window_.draw(edgeRing);
        }

        // Time ring (subtle outer ring with marks)
        if (hermitRingAlpha_ > 0.005f) {
            const float ringR = hermitPondRadius_ + 15.0f;
            const auto ra = static_cast<std::uint8_t>(hermitRingAlpha_ * 120.0f);
            sf::CircleShape ring(ringR);
            ring.setOrigin({ringR, ringR});
            ring.setPosition(pondCenter);
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineColor(sf::Color(120, 160, 210, ra));
            ring.setOutlineThickness(1.5f);
            window_.draw(ring);

            // 12 tick marks around the ring
            for (int i = 0; i < 12; ++i) {
                const float angle = static_cast<float>(i) / 12.0f * 2.0f * 3.14159265f;
                const float tickLen = (i % 3 == 0) ? 8.0f : 4.0f;
                const float ir = ringR - 3.0f;
                const float or_ = ringR + tickLen;
                sf::VertexArray tick(sf::PrimitiveType::Lines, 2);
                tick[0].position = {pondCenter.x + std::cos(angle) * ir,
                                    pondCenter.y + std::sin(angle) * ir};
                tick[1].position = {pondCenter.x + std::cos(angle) * or_,
                                    pondCenter.y + std::sin(angle) * or_};
                tick[0].color = sf::Color(140, 180, 220, ra);
                tick[1].color = sf::Color(140, 180, 220, static_cast<std::uint8_t>(ra * 0.4f));
                window_.draw(tick);
            }
        }

        // Ripples
        for (const auto& r : hermitRipples_) {
            if (r.alpha <= 0.005f || r.radius <= 0.0f) continue;
            const auto ra = static_cast<std::uint8_t>(r.alpha * 130.0f);
            sf::CircleShape ripple(r.radius);
            ripple.setOrigin({r.radius, r.radius});
            ripple.setPosition(pondCenter);
            ripple.setFillColor(sf::Color::Transparent);
            ripple.setOutlineColor(sf::Color(80, 120, 180, ra));
            ripple.setOutlineThickness(1.5f);
            window_.draw(ripple);
            // Inner ring for depth
            if (r.radius > 20.0f) {
                sf::CircleShape inner(r.radius - 12.0f);
                inner.setOrigin({r.radius - 12.0f, r.radius - 12.0f});
                inner.setPosition(pondCenter);
                inner.setFillColor(sf::Color::Transparent);
                inner.setOutlineColor(sf::Color(100, 140, 200, static_cast<std::uint8_t>(ra * 0.5f)));
                inner.setOutlineThickness(1.0f);
                window_.draw(inner);
            }
        }

        // Lantern particles
        if (!hermitLanterns_.empty() && hermitLanternTex_.getSize().x > 0) {
            const auto lsz = sf::Vector2f(hermitLanternTex_.getSize());
            for (const auto& hl : hermitLanterns_) {
                const float lifeRatio = 1.0f - hl.life / hl.maxLife;
                const float pulse = 0.7f + 0.3f * std::sin(hl.glowPhase + hermitAnimClock_.getElapsedTime().asSeconds() * 2.5f);
                const auto alpha = static_cast<std::uint8_t>(lifeRatio * pulse * 160.0f);
                if (alpha == 0) continue;
                sf::Sprite lantern(hermitLanternTex_);
                lantern.setOrigin({lsz.x * 0.5f, lsz.y * 0.5f});
                lantern.setPosition(hl.pos);
                lantern.setScale({hl.scale * pulse, hl.scale * pulse});
                lantern.setColor(sf::Color(160, 190, 230, alpha));
                window_.draw(lantern);
            }
        }
    }

    // Hierophant card animation rendering (Rule Binding)
    const bool hierophantVisible = hierophantAnimPending_ || (hierophantCornerAlpha_ > 0.005f);
    if (hierophantVisible) {
        // Compute 9x9 zone boundaries (center of board, cells 3-11)
        const float zLeft = board_.cellToPixel(0, 3).x - 15.0f;
        const float zRight = board_.cellToPixel(0, 11).x + 15.0f;
        const float zTop = board_.cellToPixel(3, 0).y - 15.0f;
        const float zBottom = board_.cellToPixel(11, 0).y + 15.0f;
        const float zW = zRight - zLeft;
        const float zH = zBottom - zTop;
        const sf::Vector2f zCenter((zLeft + zRight) * 0.5f, (zTop + zBottom) * 0.5f);

        // Outer dim overlay — four rectangles around the 9x9 zone
        const float outerDim = hierophantAnimPending_ ? hierophantOuterDimAlpha_ : 0.0f;
        if (outerDim > 0.005f) {
            const auto odAlpha = static_cast<std::uint8_t>(outerDim * 100.0f);
            const float bL = board_.cellToPixel(0, 0).x - 12.0f;
            const float bR = board_.cellToPixel(0, Board::kBoardSize - 1).x + 12.0f;
            const float bT = board_.cellToPixel(0, 0).y - 12.0f;
            const float bB = board_.cellToPixel(Board::kBoardSize - 1, 0).y + 12.0f;
            // Top strip
            { sf::RectangleShape r({bR - bL, zTop - bT}); r.setPosition({bL, bT}); r.setFillColor(sf::Color(20, 15, 10, odAlpha)); window_.draw(r); }
            // Bottom strip
            { sf::RectangleShape r({bR - bL, bB - zBottom}); r.setPosition({bL, zBottom}); r.setFillColor(sf::Color(20, 15, 10, odAlpha)); window_.draw(r); }
            // Left strip
            { sf::RectangleShape r({zLeft - bL, zH}); r.setPosition({bL, zTop}); r.setFillColor(sf::Color(20, 15, 10, odAlpha)); window_.draw(r); }
            // Right strip
            { sf::RectangleShape r({bR - zRight, zH}); r.setPosition({zRight, zTop}); r.setFillColor(sf::Color(20, 15, 10, odAlpha)); window_.draw(r); }
        }

        // Sacred boundary — four golden walls around 9x9 zone
        const float boundaryAlpha = hierophantAnimPending_ ? hierophantBoundaryAlpha_ : hierophantCornerAlpha_ * 0.5f;
        if (boundaryAlpha > 0.005f) {
            const auto ba = static_cast<std::uint8_t>(boundaryAlpha * 200.0f);
            const float thickness = hierophantAnimPending_ ? 2.5f : 1.5f;
            // Top wall
            { sf::RectangleShape wall({zW, thickness}); wall.setPosition({zLeft, zTop}); wall.setFillColor(sf::Color(232, 200, 80, ba)); window_.draw(wall); }
            // Bottom wall
            { sf::RectangleShape wall({zW, thickness}); wall.setPosition({zLeft, zBottom - thickness}); wall.setFillColor(sf::Color(232, 200, 80, ba)); window_.draw(wall); }
            // Left wall
            { sf::RectangleShape wall({thickness, zH}); wall.setPosition({zLeft, zTop}); wall.setFillColor(sf::Color(232, 200, 80, ba)); window_.draw(wall); }
            // Right wall
            { sf::RectangleShape wall({thickness, zH}); wall.setPosition({zRight - thickness, zTop}); wall.setFillColor(sf::Color(232, 200, 80, ba)); window_.draw(wall); }
        }

        // Corner pillar markers — four golden diamonds at zone corners
        const float cornerAlpha = hierophantAnimPending_ ? hierophantCornerAlpha_ : hierophantCornerAlpha_;
        if (cornerAlpha > 0.005f) {
            const auto ca = static_cast<std::uint8_t>(cornerAlpha * 220.0f);
            const float cs = 6.0f;
            const sf::Vector2f corners[4] = {
                {zLeft, zTop}, {zRight, zTop}, {zLeft, zBottom}, {zRight, zBottom}
            };
            for (const auto& c : corners) {
                // Diamond shape at corner
                sf::VertexArray diamond(sf::PrimitiveType::TriangleFan, 4);
                diamond[0].position = {c.x, c.y - cs};
                diamond[1].position = {c.x + cs, c.y};
                diamond[2].position = {c.x, c.y + cs};
                diamond[3].position = {c.x - cs, c.y};
                diamond[0].color = sf::Color(240, 210, 90, ca);
                diamond[1].color = sf::Color(200, 170, 60, ca);
                diamond[2].color = sf::Color(240, 210, 90, ca);
                diamond[3].color = sf::Color(200, 170, 60, ca);
                window_.draw(diamond);
                // Outer glow ring
                sf::CircleShape glow(cs + 4.0f);
                glow.setOrigin({cs + 4.0f, cs + 4.0f});
                glow.setPosition(c);
                glow.setFillColor(sf::Color::Transparent);
                glow.setOutlineColor(sf::Color(232, 200, 80, static_cast<std::uint8_t>(ca * 0.5f)));
                glow.setOutlineThickness(1.5f);
                window_.draw(glow);
            }
        }

        // Holy light pillar at center (animation phase 0-2 only)
        if (hierophantAnimPending_ && hierophantLightAlpha_ > 0.005f && hierophantLightTex_.getSize().x > 0) {
            const auto la = static_cast<std::uint8_t>(hierophantLightAlpha_ * 200.0f);
            const auto lsz = sf::Vector2f(hierophantLightTex_.getSize());
            sf::Sprite light(hierophantLightTex_);
            light.setOrigin({lsz.x * 0.5f, lsz.y * 0.5f});
            light.setPosition(zCenter);
            const float lightScale = (zH * 0.8f) / lsz.y;
            light.setScale({lightScale * 0.5f, lightScale});
            light.setColor(sf::Color(255, 255, 240, la));
            window_.draw(light);
        }

        // Sacred seal/cross at tengen (animation phase 1-2 only)
        if (hierophantAnimPending_ && hierophantSealAlpha_ > 0.005f && hierophantRuneTex_.getSize().x > 0) {
            const auto sa = static_cast<std::uint8_t>(hierophantSealAlpha_ * 200.0f);
            const auto rsz = sf::Vector2f(hierophantRuneTex_.getSize());
            const sf::Vector2f tengenPx = board_.cellToPixel(7, 7);
            sf::Sprite rune(hierophantRuneTex_);
            rune.setOrigin({rsz.x * 0.5f, rsz.y * 0.5f});
            rune.setPosition(tengenPx);
            const float sealScale = 1.8f + hierophantSealAlpha_ * 0.5f;
            rune.setScale({sealScale, sealScale});
            rune.setColor(sf::Color(255, 240, 180, sa));
            window_.draw(rune);
        }

        // Golden particles (animation phase 1-2 only)
        if (hierophantAnimPending_ && !hierophantParticles_.empty() && hierophantLightTex_.getSize().x > 0) {
            for (const auto& hp : hierophantParticles_) {
                const float lifeRatio = 1.0f - hp.life / hp.maxLife;
                const auto alpha = static_cast<std::uint8_t>(lifeRatio * 150.0f);
                if (alpha == 0) continue;
                sf::CircleShape dot(hp.scale * lifeRatio * 3.0f);
                dot.setOrigin({hp.scale * lifeRatio * 3.0f, hp.scale * lifeRatio * 3.0f});
                dot.setPosition(hp.pos);
                dot.setFillColor(sf::Color(240, 210, 100, alpha));
                window_.draw(dot);
            }
        }
    }

    // Justice card animation rendering (Heavenly Cycle)
    if (justiceAnimPending_ && justiceScaleTex_.getSize().x > 0) {
        const sf::Vector2f boardCenter = board_.cellToPixel(7, 7);

        // Screen brightening overlay (cool white, subtle)
        if (justiceScreenBrightAlpha_ > 0.005f) {
            sf::RectangleShape overlay({1280.0f, 820.0f});
            overlay.setPosition({0.0f, 0.0f});
            overlay.setFillColor(sf::Color(240, 238, 245,
                static_cast<std::uint8_t>(justiceScreenBrightAlpha_ * 255.0f)));
            window_.draw(overlay);
        }

        // Targeted piece glow
        if (justicePieceGlowAlpha_ > 0.005f && !justicePiecesRemoved_) {
            const float cellR = (board_.cellToPixel(0, 1).x - board_.cellToPixel(0, 0).x) * 0.48f;
            const auto drawPieceGlow = [&](sf::Vector2i gridPos, sf::Color glowColor) {
                if (gridPos.x < 0) return;
                const auto px = board_.cellToPixel(gridPos.x, gridPos.y);
                const auto alpha = static_cast<std::uint8_t>(justicePieceGlowAlpha_ * 220.0f);
                // Outer glow ring
                sf::CircleShape glow(cellR + 8.0f);
                glow.setOrigin({cellR + 8.0f, cellR + 8.0f});
                glow.setPosition(px);
                glow.setFillColor(sf::Color(glowColor.r, glowColor.g, glowColor.b,
                    static_cast<std::uint8_t>(alpha * 0.4f)));
                window_.draw(glow);
                // Inner bright ring
                sf::CircleShape inner(cellR + 2.0f);
                inner.setOrigin({cellR + 2.0f, cellR + 2.0f});
                inner.setPosition(px);
                inner.setFillColor(sf::Color::Transparent);
                inner.setOutlineColor(sf::Color(glowColor.r, glowColor.g, glowColor.b, alpha));
                inner.setOutlineThickness(2.5f);
                window_.draw(inner);
            };
            drawPieceGlow(justiceBlackPiece_, sf::Color(140, 80, 200));   // dark purple for black
            drawPieceGlow(justiceWhitePiece_, sf::Color(240, 210, 100));  // gold for white
        }

        // Scales of justice at board center
        if (justiceScaleAlpha_ > 0.005f) {
            const auto sa = static_cast<std::uint8_t>(justiceScaleAlpha_ * 200.0f);
            const auto ssz = sf::Vector2f(justiceScaleTex_.getSize());
            sf::Sprite scale(justiceScaleTex_);
            scale.setOrigin({ssz.x * 0.5f, ssz.y * 0.5f});
            scale.setPosition(boardCenter);
            scale.setScale({3.5f, 3.5f});
            scale.setColor(sf::Color(255, 240, 200, sa));
            window_.draw(scale);
            // Scale glow
            sf::CircleShape scaleGlow(50.0f);
            scaleGlow.setOrigin({50.0f, 50.0f});
            scaleGlow.setPosition(boardCenter);
            scaleGlow.setFillColor(sf::Color(240, 220, 150, static_cast<std::uint8_t>(sa * 0.25f)));
            window_.draw(scaleGlow);
        }

        // Light beams on targeted pieces
        if (justiceLightAlpha_ > 0.005f && !justicePiecesRemoved_) {
            const auto drawLightBeam = [&](sf::Vector2i gridPos, sf::Color beamColor) {
                if (gridPos.x < 0) return;
                const auto px = board_.cellToPixel(gridPos.x, gridPos.y);
                const auto alpha = static_cast<std::uint8_t>(justiceLightAlpha_ * 150.0f);
                const float beamW = 8.0f + justiceLightAlpha_ * 4.0f;
                sf::VertexArray beam(sf::PrimitiveType::TriangleStrip, 4);
                beam[0].position = {px.x - beamW, px.y - 60.0f};
                beam[1].position = {px.x + beamW, px.y - 60.0f};
                beam[2].position = {px.x - beamW * 0.3f, px.y};
                beam[3].position = {px.x + beamW * 0.3f, px.y};
                const auto topColor = sf::Color(beamColor.r, beamColor.g, beamColor.b, static_cast<std::uint8_t>(alpha * 0.1f));
                const auto botColor = sf::Color(beamColor.r, beamColor.g, beamColor.b, alpha);
                beam[0].color = topColor;
                beam[1].color = topColor;
                beam[2].color = botColor;
                beam[3].color = botColor;
                window_.draw(beam);
            };
            drawLightBeam(justiceBlackPiece_, sf::Color(160, 100, 220));
            drawLightBeam(justiceWhitePiece_, sf::Color(250, 230, 140));
        }

        // Feathers
        if (!justiceFeathers_.empty() && justiceFeatherTex_.getSize().x > 0) {
            const auto fsz = sf::Vector2f(justiceFeatherTex_.getSize());
            for (const auto& jf : justiceFeathers_) {
                const float lifeRatio = 1.0f - jf.life / jf.maxLife;
                const auto alpha = static_cast<std::uint8_t>(lifeRatio * 140.0f);
                if (alpha == 0) continue;
                sf::Sprite feather(justiceFeatherTex_);
                feather.setOrigin({fsz.x * 0.5f, fsz.y * 0.5f});
                feather.setPosition(jf.pos);
                feather.setScale({jf.scale * lifeRatio, jf.scale * lifeRatio});
                feather.setRotation(sf::degrees(jf.rotation));
                feather.setColor(sf::Color(245, 242, 235, alpha));
                window_.draw(feather);
            }
        }
    }

    // Hanged Man card animation rendering (Self-Sacrifice)
    if (hangedManAnimPending_) {
        // Dark crimson overlay
        if (hangedManOverlayAlpha_ > 0.005f) {
            sf::RectangleShape overlay({1280.0f, 820.0f});
            overlay.setPosition({0.0f, 0.0f});
            overlay.setFillColor(sf::Color(100, 20, 20,
                static_cast<std::uint8_t>(hangedManOverlayAlpha_ * 255.0f)));
            window_.draw(overlay);
        }

        // Helper to get cell pixel center
        const auto cellPx = [&](sf::Vector2i gp) { return board_.cellToPixel(gp.x, gp.y); };

        // Sacrifice piece glow (before removal)
        if (!hangedManSacrificed_ && hangedManSacrificeGlowAlpha_ > 0.005f && hangedManSacrificePos_.x >= 0) {
            const auto spx = cellPx(hangedManSacrificePos_);
            const float cellR = (board_.cellToPixel(0, 1).x - board_.cellToPixel(0, 0).x) * 0.45f;
            const auto alpha = static_cast<std::uint8_t>(hangedManSacrificeGlowAlpha_ * 180.0f);
            // Crimson glow disc
            sf::CircleShape glow(cellR + 6.0f);
            glow.setOrigin({cellR + 6.0f, cellR + 6.0f});
            glow.setPosition(spx);
            glow.setFillColor(sf::Color(180, 40, 40, static_cast<std::uint8_t>(alpha * 0.5f)));
            window_.draw(glow);
            // Inner ring
            sf::CircleShape ring(cellR + 2.0f);
            ring.setOrigin({cellR + 2.0f, cellR + 2.0f});
            ring.setPosition(spx);
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineColor(sf::Color(210, 50, 50, alpha));
            ring.setOutlineThickness(2.5f);
            window_.draw(ring);
            // Spectral thread descending from above
            if (hangedManThreadProgress_ < 0.05f) {
                const float threadAlpha = (1.0f - hangedManThreadProgress_ / 0.05f) * 80.0f;
                sf::VertexArray thread(sf::PrimitiveType::Lines, 2);
                thread[0].position = {spx.x, spx.y - 80.0f};
                thread[1].position = {spx.x, spx.y};
                thread[0].color = sf::Color(200, 60, 60, static_cast<std::uint8_t>(threadAlpha));
                thread[1].color = sf::Color(200, 60, 60, static_cast<std::uint8_t>(threadAlpha * 1.5f));
                window_.draw(thread);
            }
        }

        // Thread chains from sacrifice to targets (Phase 1)
        if (hangedManThreadProgress_ > 0.0f && hangedManSacrificePos_.x >= 0) {
            const auto spx = cellPx(hangedManSacrificePos_);
            const auto tAlpha = static_cast<std::uint8_t>(hangedManThreadProgress_ * 200.0f);
            const auto drawThread = [&](sf::Vector2i tg) {
                if (tg.x < 0) return;
                const auto tpx = cellPx(tg);
                // Bezier-curved thick line
                const sf::Vector2f mid = {
                    (spx.x + tpx.x) * 0.5f + (spx.y - tpx.y) * 0.25f,
                    (spx.y + tpx.y) * 0.5f - 40.0f
                };
                sf::VertexArray curve(sf::PrimitiveType::TriangleStrip, 20);
                const float thickness = 2.5f + hangedManThreadProgress_ * 1.5f;
                for (int i = 0; i < 10; ++i) {
                    const float t0 = static_cast<float>(i) / 9.0f;
                    const float t1 = static_cast<float>(i + 1) / 9.0f;
                    const auto bezier = [](float t, sf::Vector2f a, sf::Vector2f b, sf::Vector2f c) {
                        return a * (1.0f - t) * (1.0f - t) + b * 2.0f * (1.0f - t) * t + c * t * t;
                    };
                    const sf::Vector2f p0 = bezier(t0, spx, mid, tpx);
                    const sf::Vector2f p1 = bezier(t1, spx, mid, tpx);
                    const sf::Vector2f dir = p1 - p0;
                    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                    const sf::Vector2f nrm = (len > 0.001f) ? sf::Vector2f(-dir.y / len * thickness, dir.x / len * thickness)
                                                             : sf::Vector2f(0.0f, 0.0f);
                    curve[i * 2].position = p0 + nrm;
                    curve[i * 2].color = sf::Color(200, 50, 50, tAlpha);
                    curve[i * 2 + 1].position = p0 - nrm;
                    curve[i * 2 + 1].color = sf::Color(160, 30, 30, tAlpha);
                }
                window_.draw(curve);
            };
            drawThread(hangedManTargetA_);
            drawThread(hangedManTargetB_);
        }

        // Target pieces glow (before removal)
        if (hangedManTargetGlowAlpha_ > 0.005f && !hangedManTargetsRemoved_) {
            const auto drawTargetGlow = [&](sf::Vector2i tg) {
                if (tg.x < 0) return;
                const auto tpx = cellPx(tg);
                const float cellR = (board_.cellToPixel(0, 1).x - board_.cellToPixel(0, 0).x) * 0.45f;
                const auto alpha = static_cast<std::uint8_t>(hangedManTargetGlowAlpha_ * 200.0f);
                sf::CircleShape glow(cellR + 5.0f);
                glow.setOrigin({cellR + 5.0f, cellR + 5.0f});
                glow.setPosition(tpx);
                glow.setFillColor(sf::Color(200, 50, 50, static_cast<std::uint8_t>(alpha * 0.4f)));
                window_.draw(glow);
                sf::CircleShape ring(cellR);
                ring.setOrigin({cellR, cellR});
                ring.setPosition(tpx);
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineColor(sf::Color(220, 60, 60, alpha));
                ring.setOutlineThickness(2.0f);
                window_.draw(ring);
            };
            drawTargetGlow(hangedManTargetA_);
            drawTargetGlow(hangedManTargetB_);
        }

        // Sacrifice mark at sacrifice position
        if (hangedManMarkAlpha_ > 0.005f && hangedManSacrificePos_.x >= 0 && hangedManMarkTex_.getSize().x > 0) {
            const auto spx = cellPx(hangedManSacrificePos_);
            const auto ma = static_cast<std::uint8_t>(hangedManMarkAlpha_ * 180.0f);
            const auto msz = sf::Vector2f(hangedManMarkTex_.getSize());
            sf::Sprite mark(hangedManMarkTex_);
            mark.setOrigin({msz.x * 0.5f, msz.y * 0.5f});
            mark.setPosition(spx);
            mark.setScale({2.0f, 2.0f});
            mark.setColor(sf::Color(220, 60, 60, ma));
            window_.draw(mark);
        }

        // Crimson particles
        for (const auto& hp : hangedManParticles_) {
            const float lifeRatio = 1.0f - hp.life / hp.maxLife;
            const auto alpha = static_cast<std::uint8_t>(lifeRatio * 200.0f);
            if (alpha == 0) continue;
            sf::CircleShape dot(hp.scale * lifeRatio * 3.0f);
            dot.setOrigin({hp.scale * lifeRatio * 3.0f, hp.scale * lifeRatio * 3.0f});
            dot.setPosition(hp.pos);
            dot.setFillColor(sf::Color(200, 50, 50, alpha));
            window_.draw(dot);
        }
    }

    // Devil card: demon contract animation
    if (devilAnimPending_ && devilTargetPos_.x >= 0) {
        const auto cellPx = [&](sf::Vector2i gp) { return board_.cellToPixel(gp.x, gp.y); };
        const auto tpx = cellPx(devilTargetPos_);
        const float halfCell = board_.cellToPixel(0, 1).x - board_.cellToPixel(0, 0).x;

        // Dark overlay
        if (devilOverlayAlpha_ > 0.005f) {
            sf::RectangleShape overlay({1280.0f, 820.0f});
            overlay.setPosition({0.0f, 0.0f});
            overlay.setFillColor(sf::Color(40, 10, 30,
                static_cast<std::uint8_t>(devilOverlayAlpha_ * 255.0f)));
            window_.draw(overlay);
        }

        // Pentagram / magic circle
        if (devilPentagramAlpha_ > 0.005f && devilPentagramTex_.getSize().x > 0) {
            const auto pa = static_cast<std::uint8_t>(devilPentagramAlpha_ * 200.0f);
            const auto psz = sf::Vector2f(devilPentagramTex_.getSize());
            sf::Sprite pentagram(devilPentagramTex_);
            pentagram.setOrigin({psz.x * 0.5f, psz.y * 0.5f});
            pentagram.setPosition(tpx);
            pentagram.setScale({devilPentagramScale_, devilPentagramScale_});
            pentagram.setRotation(sf::radians(devilPentagramAngle_));
            // Pulsing glow color
            const float pulse = 0.8f + 0.2f * std::sin(devilPentagramAngle_ * 3.0f);
            pentagram.setColor(sf::Color(
                static_cast<std::uint8_t>(180.0f * pulse),
                static_cast<std::uint8_t>(30.0f * pulse),
                static_cast<std::uint8_t>(60.0f * pulse), pa));
            window_.draw(pentagram);
        }

        // Target piece glow (before removal) — dark red pulsing
        if (!devilPieceRemoved_ && devilPentagramAlpha_ > 0.005f) {
            const float glowPulse = 0.6f + 0.4f * std::sin(devilPentagramAngle_ * 3.0f);
            const auto ga = static_cast<std::uint8_t>(devilPentagramAlpha_ * glowPulse * 160.0f);
            const float radius = halfCell * 0.7f;
            sf::CircleShape glow(radius);
            glow.setOrigin({radius, radius});
            glow.setPosition(tpx);
            glow.setFillColor(sf::Color(80, 10, 20, static_cast<std::uint8_t>(ga * 0.6f)));
            window_.draw(glow);
            // Ring
            sf::CircleShape ring(radius * 1.1f);
            ring.setOrigin({radius * 1.1f, radius * 1.1f});
            ring.setPosition(tpx);
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineColor(sf::Color(130, 20, 40, ga));
            ring.setOutlineThickness(2.0f);
            window_.draw(ring);
        }

        // Scorch mark (after removal)
        if (devilScorchAlpha_ > 0.005f) {
            const auto sa = static_cast<std::uint8_t>(devilScorchAlpha_ * 180.0f);
            const float sr = halfCell * 0.5f;
            sf::CircleShape scorch(sr);
            scorch.setOrigin({sr, sr});
            scorch.setPosition(tpx);
            scorch.setFillColor(sf::Color(30, 5, 10, sa));
            window_.draw(scorch);
        }

        // Particles: dark flame and embers
        for (const auto& dp : devilParticles_) {
            const float lifeRatio = 1.0f - dp.life / dp.maxLife;
            const auto alpha = static_cast<std::uint8_t>(lifeRatio * 220.0f);
            if (alpha == 0) continue;
            const float size = dp.scale * lifeRatio * 3.5f;
            sf::CircleShape dot(size);
            dot.setOrigin({size, size});
            dot.setPosition(dp.pos);
            if (dp.isEmber) {
                // Ember ash: dark red → dim orange
                const auto r = static_cast<std::uint8_t>(160.0f * lifeRatio + 60.0f);
                const auto g = static_cast<std::uint8_t>(30.0f * lifeRatio + 20.0f);
                const auto b = static_cast<std::uint8_t>(10.0f * lifeRatio);
                dot.setFillColor(sf::Color(r, g, b, alpha));
            } else {
                // Dark flame: deep purple → dark red
                const auto r = static_cast<std::uint8_t>(100.0f + 60.0f * lifeRatio);
                const auto g = static_cast<std::uint8_t>(10.0f + 20.0f * lifeRatio);
                const auto b = static_cast<std::uint8_t>(40.0f * (1.0f - lifeRatio));
                dot.setFillColor(sf::Color(r, g, b, alpha));
            }
            window_.draw(dot);
        }
    }

    // Chariot card: charge & push animation
    if (chariotAnimPending_) {
        const auto cellPx = [&](sf::Vector2i gp) { return board_.cellToPixel(gp.x, gp.y); };
        const float halfCell = board_.cellToPixel(0, 1).x - board_.cellToPixel(0, 0).x;

        // Warm golden overlay
        if (chariotOverlayAlpha_ > 0.005f) {
            sf::RectangleShape overlay({1280.0f, 820.0f});
            overlay.setPosition({0.0f, 0.0f});
            overlay.setFillColor(sf::Color(60, 40, 10,
                static_cast<std::uint8_t>(chariotOverlayAlpha_ * 255.0f)));
            window_.draw(overlay);
        }

        // Golden gear at push source
        if (chariotChargeAlpha_ > 0.005f && chariotPushSource_.x >= 0 && chariotGearTex_.getSize().x > 0) {
            const auto spx = cellPx(chariotPushSource_);
            const auto ga = static_cast<std::uint8_t>(chariotChargeAlpha_ * 220.0f);
            const auto gsz = sf::Vector2f(chariotGearTex_.getSize());
            const float gearScale = 0.8f + chariotChargeAlpha_ * 1.0f;
            sf::Sprite gear(chariotGearTex_);
            gear.setOrigin({gsz.x * 0.5f, gsz.y * 0.5f});
            gear.setPosition(spx);
            gear.setScale({gearScale, gearScale});
            gear.setRotation(sf::radians(chariotGearAngle_));
            gear.setColor(sf::Color(240, 180, 40, ga));
            window_.draw(gear);
        }

        // Impact flash ring
        if (chariotImpactAlpha_ > 0.005f && chariotPushSource_.x >= 0) {
            const auto spx = cellPx(chariotPushSource_);
            const auto ia = static_cast<std::uint8_t>(chariotImpactAlpha_ * 200.0f);
            const float ir = halfCell * 1.2f;
            sf::CircleShape ring(ir);
            ring.setOrigin({ir, ir});
            ring.setPosition(spx);
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineColor(sf::Color(255, 200, 50, ia));
            ring.setOutlineThickness(3.0f);
            window_.draw(ring);
        }

        // Push trail from source to dest
        if (chariotTrailAlpha_ > 0.005f && chariotPushSource_.x >= 0 && chariotPushDest_.x >= 0) {
            const auto spx = cellPx(chariotPushSource_);
            const auto dpx = cellPx(chariotPushDest_);
            const auto ta = static_cast<std::uint8_t>(chariotTrailAlpha_ * 180.0f);
            const sf::Vector2f dir = {static_cast<float>(dpx.x - spx.x), static_cast<float>(dpx.y - spx.y)};
            const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            const sf::Vector2f nrm = len > 0.001f ? sf::Vector2f(dir.x / len, dir.y / len) : sf::Vector2f(1.0f, 0.0f);
            const sf::Vector2f perp = {-nrm.y * 4.0f, nrm.x * 4.0f};
            sf::VertexArray trail(sf::PrimitiveType::TriangleStrip, 4);
            trail[0].position = spx + perp;
            trail[1].position = spx - perp;
            trail[2].position = dpx + perp;
            trail[3].position = dpx - perp;
            trail[0].color = sf::Color(255, 200, 50, ta);
            trail[1].color = sf::Color(255, 200, 50, ta);
            trail[2].color = sf::Color(255, 180, 30, static_cast<std::uint8_t>(ta * 0.5f));
            trail[3].color = sf::Color(255, 180, 30, static_cast<std::uint8_t>(ta * 0.5f));
            window_.draw(trail);
        }

        // Particles
        for (const auto& cp : chariotParticles_) {
            const float lifeRatio = 1.0f - cp.life / cp.maxLife;
            const auto alpha = static_cast<std::uint8_t>(lifeRatio * 200.0f);
            if (alpha == 0) continue;
            const float size = cp.scale * lifeRatio * 3.0f;
            sf::CircleShape dot(size);
            dot.setOrigin({size, size});
            dot.setPosition(cp.pos);
            dot.setFillColor(sf::Color(255, 200, 50, alpha));
            window_.draw(dot);
        }
    }

    // Magician card: mirror image animation
    if (magicianAnimPending_ && magicianSourcePos_.x >= 0 && magicianTargetPos_.x >= 0) {
        const auto cellPx = [&](sf::Vector2i gp) { return board_.cellToPixel(gp.x, gp.y); };
        const auto spx = cellPx(magicianSourcePos_);
        const auto tpx = cellPx(magicianTargetPos_);
        const float halfCell = board_.cellToPixel(0, 1).x - board_.cellToPixel(0, 0).x;

        // Purple-gold overlay
        if (magicianOverlayAlpha_ > 0.005f) {
            sf::RectangleShape overlay({1280.0f, 820.0f});
            overlay.setPosition({0.0f, 0.0f});
            overlay.setFillColor(sf::Color(30, 10, 50,
                static_cast<std::uint8_t>(magicianOverlayAlpha_ * 255.0f)));
            window_.draw(overlay);
        }

        // Ripple rings at source
        if (magicianRippleAlpha_ > 0.005f) {
            const auto ra = static_cast<std::uint8_t>(magicianRippleAlpha_ * 180.0f);
            for (int layer = 0; layer < 3; ++layer) {
                const float rippleR = halfCell * (0.5f + layer * 0.6f + magicianRippleAlpha_ * 1.5f);
                sf::CircleShape ring(rippleR);
                ring.setOrigin({rippleR, rippleR});
                ring.setPosition(spx);
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineColor(sf::Color(180, 100, 255,
                    static_cast<std::uint8_t>(ra * (1.0f - layer * 0.3f) * (1.0f - magicianRippleAlpha_ * 0.5f))));
                ring.setOutlineThickness(2.0f - layer * 0.5f);
                window_.draw(ring);
            }
        }

        // Magic arc: bezier curve ribbon from source to target
        if (magicianArcProgress_ > 0.005f) {
            const sf::Vector2f mid = {
                (spx.x + tpx.x) * 0.5f,
                (spx.y + tpx.y) * 0.5f - 80.0f
            };
            const int segments = 20;
            sf::VertexArray arc(sf::PrimitiveType::TriangleStrip, segments * 2);
            const float thickness = 4.0f + magicianArcProgress_ * 2.0f;
            const auto bezier = [](float t, sf::Vector2f a, sf::Vector2f b, sf::Vector2f c) {
                return a * (1.0f - t) * (1.0f - t) + b * 2.0f * (1.0f - t) * t + c * t * t;
            };
            const auto alpha = static_cast<std::uint8_t>(magicianArcProgress_ * 200.0f);
            for (int i = 0; i < segments; ++i) {
                const float t0 = static_cast<float>(i) / static_cast<float>(segments - 1);
                const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments - 1);
                // Only draw up to current progress
                if (t0 > magicianArcProgress_) break;
                const float clampedT1 = std::min(t1, magicianArcProgress_);
                const sf::Vector2f p0 = bezier(t0, spx, mid, tpx);
                const sf::Vector2f p1 = bezier(clampedT1, spx, mid, tpx);
                const sf::Vector2f dir = p1 - p0;
                const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                const sf::Vector2f nrm = (len > 0.001f) ? sf::Vector2f(-dir.y / len * thickness, dir.x / len * thickness)
                                                         : sf::Vector2f(0.0f, thickness);
                arc[i * 2].position = p0 + nrm;
                arc[i * 2].color = sf::Color(160, 80, 240, alpha);
                arc[i * 2 + 1].position = p0 - nrm;
                arc[i * 2 + 1].color = sf::Color(240, 180, 40, alpha);
            }
            window_.draw(arc);
        }

        // Portal ring at target
        if (magicianPortalAlpha_ > 0.005f) {
            const auto pa = static_cast<std::uint8_t>(magicianPortalAlpha_ * 200.0f);
            const float pulse = 0.8f + 0.2f * std::sin(magicianPortalAlpha_ * 4.0f * 3.14159265f);
            const float pr = halfCell * (1.0f + magicianPortalAlpha_ * 0.5f);
            sf::CircleShape portal(pr);
            portal.setOrigin({pr, pr});
            portal.setPosition(tpx);
            portal.setFillColor(sf::Color::Transparent);
            portal.setOutlineColor(sf::Color(
                static_cast<std::uint8_t>(160.0f * pulse),
                static_cast<std::uint8_t>(80.0f * pulse),
                static_cast<std::uint8_t>(240.0f * pulse), pa));
            portal.setOutlineThickness(2.5f);
            window_.draw(portal);
            // Inner glow
            sf::CircleShape inner(pr * 0.7f);
            inner.setOrigin({pr * 0.7f, pr * 0.7f});
            inner.setPosition(tpx);
            inner.setFillColor(sf::Color(120, 60, 200, static_cast<std::uint8_t>(pa * 0.3f)));
            window_.draw(inner);
        }

        // Piece materialization glow at target
        if (magicianSpawnAlpha_ > 0.005f) {
            const auto sa = static_cast<std::uint8_t>(magicianSpawnAlpha_ * 220.0f);
            const float sr = halfCell * (1.5f - magicianSpawnAlpha_ * 0.5f);
            sf::CircleShape glow(sr);
            glow.setOrigin({sr, sr});
            glow.setPosition(tpx);
            glow.setFillColor(sf::Color(200, 150, 40, static_cast<std::uint8_t>(sa * 0.4f)));
            window_.draw(glow);
        }

        // Particles
        for (const auto& mp : magicianParticles_) {
            const float lifeRatio = 1.0f - mp.life / mp.maxLife;
            const auto alpha = static_cast<std::uint8_t>(lifeRatio * 210.0f);
            if (alpha == 0) continue;
            const float size = mp.scale * lifeRatio * 3.0f;
            if (mp.type == 0) {
                // Diamond shape
                sf::VertexArray diamond(sf::PrimitiveType::TriangleFan, 4);
                const sf::Vector2f c = mp.pos;
                diamond[0].position = {c.x, c.y - size * 1.3f};
                diamond[1].position = {c.x + size, c.y};
                diamond[2].position = {c.x, c.y + size * 1.3f};
                diamond[3].position = {c.x - size, c.y};
                for (int v = 0; v < 4; ++v) {
                    diamond[v].color = sf::Color(200, 150, 40, alpha);
                }
                window_.draw(diamond);
            } else {
                // Sparkle (small cross/star)
                sf::CircleShape dot(size * 0.7f);
                dot.setOrigin({size * 0.7f, size * 0.7f});
                dot.setPosition(mp.pos);
                dot.setFillColor(sf::Color(180, 120, 255, alpha));
                window_.draw(dot);
            }
        }
    }

    // Temperance card: harmony activation animation
    if (temperanceAnimPending_) {
        const auto boardCenter = board_.cellToPixel(7, 7);

        // Soft blue-gold overlay
        if (temperanceOverlayAlpha_ > 0.005f) {
            sf::RectangleShape overlay({1280.0f, 820.0f});
            overlay.setPosition({0.0f, 0.0f});
            overlay.setFillColor(sf::Color(20, 30, 60,
                static_cast<std::uint8_t>(temperanceOverlayAlpha_ * 255.0f)));
            window_.draw(overlay);
        }

        // Angel wings — two curved wing shapes from upper sides converging to center
        if (temperanceWingAlpha_ > 0.005f) {
            const auto wa = static_cast<std::uint8_t>(temperanceWingAlpha_ * 160.0f);

            const auto drawWing = [&](bool left) {
                sf::VertexArray wing(sf::PrimitiveType::TriangleFan, 7);
                const float sideX = left ? 180.0f : 1100.0f;
                const float topY = 20.0f - temperanceWingAlpha_ * 40.0f;
                const float dirX = left ? 1.0f : -1.0f;

                wing[0].position = {sideX, topY};
                wing[0].color = sf::Color(100, 160, 220, static_cast<std::uint8_t>(wa * 0.5f));
                wing[1].position = {sideX - dirX * 120.0f, topY + 200.0f};
                wing[1].color = sf::Color(60, 120, 200, static_cast<std::uint8_t>(wa * 0.7f));
                wing[2].position = {sideX - dirX * 100.0f, topY + 320.0f};
                wing[2].color = sf::Color(40, 80, 180, static_cast<std::uint8_t>(wa * 0.5f));
                wing[3].position = {boardCenter.x, boardCenter.y - 60.0f};
                wing[3].color = sf::Color(180, 160, 80, static_cast<std::uint8_t>(wa * 0.3f));
                wing[4].position = {boardCenter.x - dirX * 60.0f, topY + 260.0f};
                wing[4].color = sf::Color(80, 140, 210, static_cast<std::uint8_t>(wa * 0.5f));
                wing[5].position = {boardCenter.x - dirX * 30.0f, topY + 150.0f};
                wing[5].color = sf::Color(120, 170, 210, static_cast<std::uint8_t>(wa * 0.6f));
                wing[6].position = {sideX - dirX * 60.0f, topY + 80.0f};
                wing[6].color = sf::Color(80, 140, 210, static_cast<std::uint8_t>(wa * 0.6f));
                window_.draw(wing);
            };
            drawWing(true);
            drawWing(false);
        }

        // Expanding halo ring
        if (temperanceHaloAlpha_ > 0.005f && temperanceHaloRadius_ > 0.0f) {
            const auto ha = static_cast<std::uint8_t>(temperanceHaloAlpha_ * 140.0f);
            sf::CircleShape halo(temperanceHaloRadius_);
            halo.setOrigin({temperanceHaloRadius_, temperanceHaloRadius_});
            halo.setPosition(boardCenter);
            halo.setFillColor(sf::Color::Transparent);
            halo.setOutlineColor(sf::Color(140, 180, 240, ha));
            halo.setOutlineThickness(3.0f);
            window_.draw(halo);
            // Inner gold ring
            sf::CircleShape inner(temperanceHaloRadius_ * 0.85f);
            inner.setOrigin({temperanceHaloRadius_ * 0.85f, temperanceHaloRadius_ * 0.85f});
            inner.setPosition(boardCenter);
            inner.setFillColor(sf::Color::Transparent);
            inner.setOutlineColor(sf::Color(200, 170, 60, static_cast<std::uint8_t>(ha * 0.5f)));
            inner.setOutlineThickness(1.5f);
            window_.draw(inner);
        }

        // Falling particles
        for (const auto& tp : temperanceParticles_) {
            const float lifeRatio = 1.0f - tp.life / tp.maxLife;
            const auto alpha = static_cast<std::uint8_t>(lifeRatio * 180.0f);
            if (alpha == 0) continue;
            const float size = tp.scale * lifeRatio * 2.5f;
            sf::CircleShape dot(size);
            dot.setOrigin({size, size});
            dot.setPosition(tp.pos);
            dot.setFillColor(sf::Color(160, 200, 240, alpha));
            window_.draw(dot);
        }
    }

    // Persistent temperance mark at board center (while effect active)
    if (temperanceRemaining_ > 0 && temperanceMarkAlpha_ > 0.005f) {
        const auto boardCenter = board_.cellToPixel(7, 7);
        const auto ma = static_cast<std::uint8_t>(temperanceMarkAlpha_ * 120.0f);
        const float mr = 14.0f;
        // Small rotating gold ring
        sf::CircleShape markRing(mr);
        markRing.setOrigin({mr, mr});
        markRing.setPosition(boardCenter);
        markRing.setFillColor(sf::Color::Transparent);
        markRing.setOutlineColor(sf::Color(200, 170, 60, ma));
        markRing.setOutlineThickness(1.8f);
        markRing.setRotation(sf::radians(temperanceMarkAngle_));
        window_.draw(markRing);
    }

    // Fool card: lucky star (activation + persistent)
    if ((foolAnimPending_ || (foolActive_ && foolStarAlpha_ > 0.005f)) && foolStarAlpha_ > 0.005f) {
        // Overlay during activation
        if (foolAnimPending_ && foolOverlayAlpha_ > 0.005f) {
            sf::RectangleShape overlay({1280.0f, 820.0f});
            overlay.setPosition({0.0f, 0.0f});
            overlay.setFillColor(sf::Color(40, 35, 20,
                static_cast<std::uint8_t>(foolOverlayAlpha_ * 255.0f)));
            window_.draw(overlay);
        }

        // Burst ring
        if (foolBurstAlpha_ > 0.005f) {
            const auto ba = static_cast<std::uint8_t>(foolBurstAlpha_ * 160.0f);
            sf::CircleShape ring(80.0f + foolBurstAlpha_ * 40.0f);
            ring.setOrigin({80.0f + foolBurstAlpha_ * 40.0f, 80.0f + foolBurstAlpha_ * 40.0f});
            ring.setPosition(foolStarPos_);
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineColor(sf::Color(255, 220, 100, ba));
            ring.setOutlineThickness(2.5f);
            window_.draw(ring);
        }

        // Four-pointed lucky star
        const auto sa = static_cast<std::uint8_t>(foolStarAlpha_ * 220.0f);
        const float sr = 12.0f * foolStarScale_;
        const float srInner = 4.0f * foolStarScale_;
        sf::VertexArray star(sf::PrimitiveType::TriangleFan, 9);
        star[0].position = foolStarPos_;
        star[0].color = sf::Color(255, 230, 80, sa);
        for (int i = 0; i < 4; ++i) {
            const float outAngle = foolStarAngle_ + static_cast<float>(i) * 3.14159265f / 2.0f;
            const float inAngle = outAngle + 3.14159265f / 4.0f;
            star[i * 2 + 1].position = {foolStarPos_.x + std::cos(outAngle) * sr,
                                         foolStarPos_.y + std::sin(outAngle) * sr};
            star[i * 2 + 1].color = sf::Color(255, 230, 80, sa);
            star[i * 2 + 2].position = {foolStarPos_.x + std::cos(inAngle) * srInner,
                                         foolStarPos_.y + std::sin(inAngle) * srInner};
            star[i * 2 + 2].color = sf::Color(255, 200, 40, static_cast<std::uint8_t>(sa * 0.6f));
        }
        window_.draw(star);
    }

    // Rainbow particles (during activation)
    if (foolAnimPending_) {
        for (const auto& fp : foolParticles_) {
            const float lifeRatio = 1.0f - fp.life / fp.maxLife;
            const auto alpha = static_cast<std::uint8_t>(lifeRatio * 200.0f);
            if (alpha == 0) continue;
            const float size = fp.scale * lifeRatio * 3.0f;
            // Hue to RGB
            const float h = fp.hue * 6.0f;
            const float c = 1.0f;
            const float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (h < 1.0f)      { r = c; g = x; b = 0.0f; }
            else if (h < 2.0f) { r = x; g = c; b = 0.0f; }
            else if (h < 3.0f) { r = 0.0f; g = c; b = x; }
            else if (h < 4.0f) { r = 0.0f; g = x; b = c; }
            else if (h < 5.0f) { r = x; g = 0.0f; b = c; }
            else               { r = c; g = 0.0f; b = x; }
            sf::CircleShape dot(size);
            dot.setOrigin({size, size});
            dot.setPosition(fp.pos);
            dot.setFillColor(sf::Color(
                static_cast<std::uint8_t>(r * 255.0f),
                static_cast<std::uint8_t>(g * 255.0f),
                static_cast<std::uint8_t>(b * 255.0f), alpha));
            window_.draw(dot);
        }
    }

    // Tower card: cataclysm animation
    if (towerAnimPending_) {
        const auto boardCenter = board_.cellToPixel(7, 7);

        // Screen flash
        if (towerFlashAlpha_ > 0.005f) {
            sf::RectangleShape flash({1280.0f, 820.0f});
            flash.setPosition({0.0f, 0.0f});
            flash.setFillColor(sf::Color(255, 250, 240,
                static_cast<std::uint8_t>(towerFlashAlpha_ * 255.0f)));
            window_.draw(flash);
        }

        // Lightning bolt
        if (towerLightningAlpha_ > 0.01f) {
            const auto la = static_cast<std::uint8_t>(towerLightningAlpha_ * 230.0f);
            const sf::Vector2f top = {boardCenter.x, 0.0f};
            const sf::Vector2f bot = {boardCenter.x, boardCenter.y};

            // Recursive lightning segment helper
            static thread_local std::mt19937 lRng(std::random_device{}());
            auto lSeed = static_cast<unsigned>(towerAnimClock_.getElapsedTime().asMilliseconds());

            const auto drawLightningSegment = [&](sf::Vector2f from, sf::Vector2f to, int depth, float /*thickness*/) {
                if (depth <= 0) return;
                // Draw the main line
                sf::VertexArray line(sf::PrimitiveType::Lines, 2);
                line[0].position = from;
                line[1].position = to;
                line[0].color = sf::Color(255, 255, 255, la);
                line[1].color = sf::Color(200, 210, 255, static_cast<std::uint8_t>(la * 0.7f));
                window_.draw(line);

                // Branch from midpoint
                if (depth >= 2) {
                    const sf::Vector2f mid = {(from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f};
                    const sf::Vector2f dir = to - from;
                    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                    const sf::Vector2f perp = {-dir.y / len * len * 0.3f, dir.x / len * len * 0.3f};
                    const sf::Vector2f branchEnd = {
                        mid.x + perp.x * (static_cast<float>(lRng() % 100) / 100.0f * 2.0f - 1.0f),
                        mid.y + perp.y * (static_cast<float>(lRng() % 100) / 100.0f * 2.0f - 1.0f)
                    };
                    sf::VertexArray branch(sf::PrimitiveType::Lines, 2);
                    branch[0].position = mid;
                    branch[1].position = branchEnd;
                    const auto ba = static_cast<std::uint8_t>(la * 0.6f);
                    branch[0].color = sf::Color(220, 230, 255, ba);
                    branch[1].color = sf::Color(180, 200, 255, static_cast<std::uint8_t>(ba * 0.4f));
                    window_.draw(branch);
                }
            };

            // Main bolt: zigzag from top to center
            const int zigSteps = 8;
            const float segLen = (bot.y - top.y) / static_cast<float>(zigSteps);
            sf::Vector2f prev = top;
            lRng.seed(lSeed);
            for (int i = 1; i <= zigSteps; ++i) {
                const sf::Vector2f curr = {
                    boardCenter.x + static_cast<float>(lRng() % 60 - 30) * (1.0f - static_cast<float>(i) / zigSteps),
                    top.y + segLen * static_cast<float>(i)
                };
                drawLightningSegment(prev, curr, i <= 3 ? 3 : (i <= 6 ? 2 : 1),
                    (zigSteps - i + 1) * 1.5f);
                prev = curr;
            }
        }

        // Tower particles: rubble (dark) or energy motes (golden)
        for (const auto& tp : towerParticles_) {
            const float lifeRatio = 1.0f - tp.life / tp.maxLife;
            const auto alpha = static_cast<std::uint8_t>(lifeRatio * 210.0f);
            if (alpha == 0) continue;
            const float size = tp.scale * lifeRatio * 3.0f;
            sf::CircleShape dot(size);
            dot.setOrigin({size, size});
            dot.setPosition(tp.pos);
            if (tp.isRubble) {
                dot.setFillColor(sf::Color(60, 50, 65, alpha));
            } else {
                dot.setFillColor(sf::Color(240, 200, 60, alpha));
            }
            window_.draw(dot);
        }
    }

    // Wheel of Fortune card: fate roulette animation
    if (wheelFortuneAnimPending_) {
        const auto boardCenter = board_.cellToPixel(7, 7);
        const float wfCx = static_cast<float>(boardCenter.x);
        const float wfCy = static_cast<float>(boardCenter.y);

        // Golden wheel
        if (wheelFortuneAlpha_ > 0.005f && wheelFortuneRadius_ > 0.0f) {
            const auto wa = static_cast<std::uint8_t>(wheelFortuneAlpha_ * 200.0f);
            const float wr = wheelFortuneRadius_;
            // Outer ring
            sf::CircleShape outer(wr);
            outer.setOrigin({wr, wr});
            outer.setPosition({wfCx, wfCy});
            outer.setFillColor(sf::Color::Transparent);
            outer.setOutlineColor(sf::Color(240, 200, 40, wa));
            outer.setOutlineThickness(3.5f);
            window_.draw(outer);
            // Inner ring
            sf::CircleShape inner(wr * 0.65f);
            inner.setOrigin({wr * 0.65f, wr * 0.65f});
            inner.setPosition({wfCx, wfCy});
            inner.setFillColor(sf::Color::Transparent);
            inner.setOutlineColor(sf::Color(200, 160, 30, static_cast<std::uint8_t>(wa * 0.6f)));
            inner.setOutlineThickness(1.5f);
            window_.draw(inner);
            // Center hub
            sf::CircleShape hub(6.0f);
            hub.setOrigin({6.0f, 6.0f});
            hub.setPosition({wfCx, wfCy});
            hub.setFillColor(sf::Color(255, 220, 60, wa));
            window_.draw(hub);
            // 8 spokes
            sf::VertexArray spokes(sf::PrimitiveType::Lines, 16);
            for (int i = 0; i < 8; ++i) {
                const float angle = wheelFortuneAngle_ + static_cast<float>(i) * 3.14159265f / 4.0f;
                const float cosA = std::cos(angle);
                const float sinA = std::sin(angle);
                spokes[i * 2].position = {wfCx + cosA * 8.0f, wfCy + sinA * 8.0f};
                spokes[i * 2 + 1].position = {wfCx + cosA * wr, wfCy + sinA * wr};
                spokes[i * 2].color = sf::Color(240, 200, 40, wa);
                spokes[i * 2 + 1].color = sf::Color(200, 160, 30, static_cast<std::uint8_t>(wa * 0.5f));
            }
            window_.draw(spokes);
        }

        // Orb trails: lines from obstacles to wheel center (Phase 2)
        if (wheelFortuneOrbProgress_ > 0.0f && wheelFortuneOrbProgress_ < 1.0f && wheelFortuneAlpha_ > 0.1f) {
            const auto oa = static_cast<std::uint8_t>(wheelFortuneOrbProgress_ * 150.0f);
            for (const auto& so : wheelFortuneSavedObstacles_) {
                const auto opx = board_.cellToPixel(static_cast<int>(so.x), static_cast<int>(so.y));
                const sf::Vector2f orbPos = {
                    static_cast<float>(opx.x) + (wfCx - static_cast<float>(opx.x)) * wheelFortuneOrbProgress_,
                    static_cast<float>(opx.y) + (wfCy - static_cast<float>(opx.y)) * wheelFortuneOrbProgress_
                };
                // Small golden orb
                sf::CircleShape orb(4.0f);
                orb.setOrigin({4.0f, 4.0f});
                orb.setPosition(orbPos);
                orb.setFillColor(sf::Color(255, 220, 40, oa));
                window_.draw(orb);
                // Trail line from original position
                sf::VertexArray trail(sf::PrimitiveType::Lines, 2);
                trail[0].position = {static_cast<float>(opx.x), static_cast<float>(opx.y)};
                trail[1].position = orbPos;
                trail[0].color = sf::Color(200, 160, 30, static_cast<std::uint8_t>(oa * 0.4f));
                trail[1].color = sf::Color(255, 220, 40, oa);
                window_.draw(trail);
            }
        }

        // Golden sparkle particles
        for (const auto& wp : wheelFortuneParticles_) {
            const float lifeRatio = 1.0f - wp.life / wp.maxLife;
            const auto alpha = static_cast<std::uint8_t>(lifeRatio * 200.0f);
            if (alpha == 0) continue;
            const float size = wp.scale * lifeRatio * 2.5f;
            sf::CircleShape dot(size);
            dot.setOrigin({size, size});
            dot.setPosition(wp.pos);
            dot.setFillColor(sf::Color(255, 220, 50, alpha));
            window_.draw(dot);
        }
    }

    // Judgement card: divine light purification render
    if (judgementAnimPending_) {
        const auto boardCenter = board_.cellToPixel(7, 7);
        const float jCx = static_cast<float>(boardCenter.x);
        const float jCy = static_cast<float>(boardCenter.y);

        // Screen darkening overlay
        if (judgementDarkenAlpha_ > 0.5f) {
            sf::RectangleShape darken({1280.0f, 820.0f});
            darken.setFillColor(sf::Color(10, 5, 25, static_cast<std::uint8_t>(judgementDarkenAlpha_)));
            window_.draw(darken);
        }

        // Central divine light beam (vertical rectangle with gradient fade)
        if (judgementLightBeamProgress_ > 0.01f) {
            const float beamWidth = 120.0f + judgementLightBeamProgress_ * 80.0f;
            const float beamAlpha = judgementLightBeamProgress_ * 200.0f;
            const auto ba = static_cast<std::uint8_t>(beamAlpha);
            const auto baHalf = static_cast<std::uint8_t>(beamAlpha * 0.5f);

            // Core beam (bright center)
            sf::VertexArray beam(sf::PrimitiveType::TriangleStrip, 4);
            beam[0].position = sf::Vector2f(jCx - beamWidth * 0.4f, 0.0f);
            beam[1].position = sf::Vector2f(jCx + beamWidth * 0.4f, 0.0f);
            beam[2].position = sf::Vector2f(jCx - beamWidth * 0.4f, 820.0f);
            beam[3].position = sf::Vector2f(jCx + beamWidth * 0.4f, 820.0f);
            beam[0].color = sf::Color(255, 245, 200, static_cast<std::uint8_t>(ba * 0.3f));
            beam[1].color = sf::Color(255, 245, 200, static_cast<std::uint8_t>(ba * 0.3f));
            beam[2].color = sf::Color(255, 245, 200, ba);
            beam[3].color = sf::Color(255, 245, 200, ba);
            window_.draw(beam);

            // Outer glow (wider, softer)
            sf::VertexArray glow(sf::PrimitiveType::TriangleStrip, 4);
            glow[0].position = sf::Vector2f(jCx - beamWidth, 0.0f);
            glow[1].position = sf::Vector2f(jCx + beamWidth, 0.0f);
            glow[2].position = sf::Vector2f(jCx - beamWidth, 820.0f);
            glow[3].position = sf::Vector2f(jCx + beamWidth, 820.0f);
            glow[0].color = sf::Color(200, 200, 240, static_cast<std::uint8_t>(ba * 0.12f));
            glow[1].color = sf::Color(200, 200, 240, static_cast<std::uint8_t>(ba * 0.12f));
            glow[2].color = sf::Color(200, 200, 240, baHalf);
            glow[3].color = sf::Color(200, 200, 240, baHalf);
            window_.draw(glow);
        }

        // Expanding ring pulses
        for (const auto& jr : judgementRings_) {
            if (jr.alpha <= 0.005f) continue;
            const auto ra = static_cast<std::uint8_t>(jr.alpha * 160.0f);
            sf::CircleShape ring(jr.radius);
            ring.setOrigin({jr.radius, jr.radius});
            ring.setPosition({jCx, jCy});
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineColor(sf::Color(255, 240, 180, ra));
            ring.setOutlineThickness(2.5f);
            window_.draw(ring);
            // Inner echo ring
            sf::CircleShape inner(jr.radius * 0.7f);
            inner.setOrigin({jr.radius * 0.7f, jr.radius * 0.7f});
            inner.setPosition({jCx, jCy});
            inner.setFillColor(sf::Color::Transparent);
            inner.setOutlineColor(sf::Color(255, 240, 180, static_cast<std::uint8_t>(ra * 0.35f)));
            inner.setOutlineThickness(1.5f);
            window_.draw(inner);
        }

        // Particles (golden-white motes)
        for (const auto& jp : judgementParticles_) {
            const float lifeRatio = jp.life / jp.maxLife;
            if (lifeRatio >= 1.0f) continue;
            const float fadeAlpha = 1.0f - lifeRatio;
            const auto alpha = static_cast<std::uint8_t>(fadeAlpha * 220.0f);
            if (alpha == 0) continue;
            const float s = jp.size * (1.0f - lifeRatio * 0.5f);
            sf::CircleShape dot(s);
            dot.setOrigin({s, s});
            dot.setPosition(jp.pos);
            // Color shifts from warm gold to white as they rise
            dot.setFillColor(sf::Color(
                static_cast<std::uint8_t>(255),
                static_cast<std::uint8_t>(230 + static_cast<int>(fadeAlpha * 25.0f)),
                static_cast<std::uint8_t>(150 + static_cast<int>(fadeAlpha * 105.0f)),
                alpha));
            window_.draw(dot);
        }
    }

    // Death card: blood corruption spread overlay
    if (deathAnimPending_ && deathCenter_.x >= 0) {
        constexpr int kSpreadOrder[9][2] = {
            {0,0}, {0,-1}, {0,1}, {-1,0}, {1,0}, {-1,-1}, {-1,1}, {1,-1}, {1,1}
        };
        const float cellHalf = (board_.cellToPixel(0, 1).x - board_.cellToPixel(0, 0).x) * 0.5f;
        const float cellSize = cellHalf * 2.0f;
        const int numInfected = std::min(9, deathSpreadStep_ + 1);
        const bool flashing = deathFlashAlpha_ > 1.0f;
        const bool preFlashing = deathFlashAlpha_ > 0.0f && !flashing;
        const float dElapsed = deathAnimClock_.getElapsedTime().asSeconds();
        constexpr float kSpreadInterval = 0.06f;
        const bool isSpreadPhase = deathSpreadStep_ < 9 && !flashing && !preFlashing;

        for (int i = 0; i < numInfected; ++i) {
            const int cr = deathCenter_.x + kSpreadOrder[i][0];
            const int cc = deathCenter_.y + kSpreadOrder[i][1];
            if (cr < 0 || cr >= Board::kBoardSize || cc < 0 || cc >= Board::kBoardSize) continue;

            const auto cellPx = board_.cellToPixel(cr, cc);
            const float cx = cellPx.x - cellHalf;
            const float cy = cellPx.y - cellHalf;

            // Cell fade-in during spread: newest cell eases in
            float cellAlphaMul = 1.0f;
            if (isSpreadPhase && i == numInfected - 1 && numInfected < 9) {
                const float stepStart = static_cast<float>(deathSpreadStep_) * kSpreadInterval;
                const float stepProg = std::min(1.0f, (dElapsed - stepStart) / kSpreadInterval);
                cellAlphaMul = 0.45f + stepProg * 0.55f; // 0.45→1.0
            }

            if (flashing) {
                // Pixel-art crimson pulse: 16×16 layered concentric squares
                const float pulse = deathFlashAlpha_ / 220.0f; // 0→1→0
                const float px = cellSize / 16.0f;
                const float ox = cx + (cellSize - px * 16.0f) * 0.5f;
                const float oy = cy + (cellSize - px * 16.0f) * 0.5f;
                const auto alpha = static_cast<std::uint8_t>(std::min(255.0f, deathFlashAlpha_));

                // 5 concentric pixel-art rings: outer dark → inner hot
                constexpr int kRingHalfSizes[5] = {8, 6, 4, 2, 1};
                const sf::Color kRingColors[5] = {
                    sf::Color(42, 5, 5),    // deep blood
                    sf::Color(90, 10, 10),  // dark crimson
                    sf::Color(168, 24, 24), // bright crimson
                    sf::Color(224, 48, 48), // scarlet
                    sf::Color(255, 104, 104) // hot core
                };

                for (int ri = 0; ri < 5; ++ri) {
                    const int hs = kRingHalfSizes[ri];
                    const float ringSize = px * static_cast<float>(hs * 2);
                    const float rox = ox + (cellSize - ringSize) * 0.5f;
                    const float roy = oy + (cellSize - ringSize) * 0.5f;
                    const auto& c = kRingColors[ri];
                    const float brightness = 0.6f + pulse * 0.4f;
                    sf::RectangleShape square;
                    square.setPosition({std::round(rox), std::round(roy)});
                    square.setSize({std::round(ringSize), std::round(ringSize)});
                    square.setFillColor(sf::Color(
                        static_cast<std::uint8_t>(std::min(255.0f, c.r * brightness)),
                        static_cast<std::uint8_t>(std::min(255.0f, c.g * brightness)),
                        static_cast<std::uint8_t>(std::min(255.0f, c.b * brightness)),
                        alpha));
                    window_.draw(square);
                }

                // Outer glow ring (pixel-art: 1px border outside)
                const float glowSize = cellSize + px * 2.0f;
                const float gox = cx - px;
                const float goy = cy - px;
                const auto glowAlpha = static_cast<std::uint8_t>(alpha * 0.35f);
                sf::RectangleShape glow;
                glow.setPosition({std::round(gox), std::round(goy)});
                glow.setSize({std::round(glowSize), std::round(glowSize)});
                glow.setFillColor(sf::Color::Transparent);
                glow.setOutlineThickness(px);
                glow.setOutlineColor(sf::Color(220, 30, 30, glowAlpha));
                window_.draw(glow);
            } else if (preFlashing) {
                // Dim red pre-glow before the flash
                const auto pa = static_cast<std::uint8_t>(deathFlashAlpha_ * cellAlphaMul);
                sf::RectangleShape cell;
                cell.setPosition({cx, cy});
                cell.setSize({cellSize, cellSize});
                cell.setFillColor(sf::Color(80, 6, 6, pa));
                window_.draw(cell);
            } else {
                // Procedural blood spread
                const float age = static_cast<float>(i) / 8.0f;
                const float fadeAlpha = (i == numInfected - 1 && i < 8) ? 0.6f : 1.0f;
                const auto a = static_cast<std::uint8_t>(180.0f * fadeAlpha * cellAlphaMul);
                const auto r = static_cast<std::uint8_t>(135.0f - age * 40.0f);
                const auto gb = static_cast<std::uint8_t>(18.0f - age * 8.0f);
                sf::RectangleShape cell;
                cell.setPosition({cx, cy});
                cell.setSize({cellSize, cellSize});
                cell.setFillColor(sf::Color(r, gb, gb, a));
                window_.draw(cell);
            }
        }

        // Screen-wide crimson vignette during flash and pre-flash
        if (deathFlashAlpha_ > 0.0f) {
            const float vignetteAlpha = flashing
                ? deathFlashAlpha_ / 220.0f * 0.18f
                : deathFlashAlpha_ / 40.0f * 0.06f;
            if (vignetteAlpha > 0.005f) {
                const auto va = static_cast<std::uint8_t>(vignetteAlpha * 255.0f);
                sf::RectangleShape vignette;
                vignette.setPosition({0.0f, 0.0f});
                vignette.setSize({1280.0f, 820.0f});
                vignette.setFillColor(sf::Color(180, 8, 8, va));
                window_.draw(vignette);
            }
        }

        // --- Consume animation: pieces sink + shrink + blood ripple ---
        if (deathConsumeProgress_ > 0.0f && !deathPieces_.empty()) {
            const float cp = deathConsumeProgress_; // 0→1

            for (const auto& dp : deathPieces_) {
                const float pieceR = cellHalf * 0.72f;

                // Sinking piece (ease-in Y offset)
                const float sinkY = cp * cp * 18.0f;
                // Shrink (ease-in, faster toward end)
                const float shrink = 1.0f - cp * cp;
                // Color: lerp from piece color to blood red
                const bool isBlack = dp.piece == Board::Piece::Black;
                const sf::Color srcColor = isBlack ? sf::Color(32, 32, 34) : sf::Color(235, 230, 218);
                const sf::Color dstColor = sf::Color(175, 18, 22);
                const sf::Color curColor(
                    static_cast<std::uint8_t>(srcColor.r + (dstColor.r - srcColor.r) * cp),
                    static_cast<std::uint8_t>(srcColor.g + (dstColor.g - srcColor.g) * cp),
                    static_cast<std::uint8_t>(srcColor.b + (dstColor.b - srcColor.b) * cp));
                const auto pieceAlpha = static_cast<std::uint8_t>((1.0f - cp * 0.7f) * 255.0f);

                // Draw shrinking sinking piece
                const float curR = pieceR * shrink;
                if (curR > 1.0f) {
                    sf::CircleShape piece(curR, 14);
                    piece.setOrigin({curR, curR});
                    piece.setPosition({dp.pixelPos.x, dp.pixelPos.y + sinkY});
                    piece.setFillColor(sf::Color(curColor.r, curColor.g, curColor.b, pieceAlpha));
                    window_.draw(piece);
                }

                // Expanding ripple ring
                const float rippleR = pieceR + cp * cellHalf * 1.6f;
                const float rippleAlpha = (1.0f - cp) * 0.7f;
                if (rippleAlpha > 0.0f && rippleR > pieceR + 1.0f) {
                    sf::CircleShape ripple(rippleR, 20);
                    ripple.setOrigin({rippleR, rippleR});
                    ripple.setPosition({dp.pixelPos.x, dp.pixelPos.y + sinkY * 0.3f});
                    ripple.setFillColor(sf::Color::Transparent);
                    ripple.setOutlineThickness(2.5f);
                    ripple.setOutlineColor(sf::Color(185, 28, 32,
                        static_cast<std::uint8_t>(rippleAlpha * 255.0f)));
                    window_.draw(ripple);
                }
            }
        }
    }

    // Lovers card animation rendering
    if (loversAnimPending_) {
        const float lElapsed = loversAnimClock_.getElapsedTime().asSeconds();
        constexpr float kRevealEnd = 0.45f;
        constexpr float kRiseEnd = 0.75f;
        constexpr float kFlightEnd = 1.45f;
        constexpr float kLandingEnd = 1.85f;
        constexpr float kSettleEnd = 2.20f;

        const auto pxA = board_.cellToPixel(loversPieceA_.x, loversPieceA_.y);
        const auto pxB = board_.cellToPixel(loversPieceB_.x, loversPieceB_.y);
        const bool isBlack = loversPieceAType_ == Board::Piece::Black;
        const sf::Color colorA = isBlack ? sf::Color(34, 34, 36) : sf::Color(238, 233, 220);
        const sf::Color colorB = isBlack ? sf::Color(238, 233, 220) : sf::Color(34, 34, 36);

        // --- Compute current piece positions ---
        sf::Vector2f curPosA = pxA;
        sf::Vector2f curPosB = pxB;

        if (lElapsed >= kRiseEnd && lElapsed < kFlightEnd) {
            const float fp = (lElapsed - kRiseEnd) / (kFlightEnd - kRiseEnd);
            const float eased = fp < 0.5f
                ? 2.0f * fp * fp
                : 1.0f - std::pow(-2.0f * fp + 2.0f, 2.0f) / 2.0f;
            const float progress = 0.05f + eased * 0.95f;
            const int idx = std::clamp(static_cast<int>(progress * 35.0f), 0, 35);
            if (!loversArcPathA_.empty() && !loversArcPathB_.empty()) {
                curPosA = loversArcPathA_[idx];
                curPosB = loversArcPathB_[idx];
            }
        } else if (lElapsed >= kFlightEnd) {
            curPosA = pxB;
            curPosB = pxA;
        }

        // Rise lift offset
        float liftA = 0.0f, liftB = 0.0f;
        if (lElapsed >= kRevealEnd && lElapsed < kRiseEnd) {
            const float rp = (lElapsed - kRevealEnd) / (kRiseEnd - kRevealEnd);
            const float eased = rp < 0.5f ? 2.0f * rp * rp : 1.0f - std::pow(-2.0f * rp + 2.0f, 2.0f) / 2.0f;
            liftA = eased * 30.0f;
            liftB = eased * 30.0f;
        } else if (lElapsed >= kRiseEnd && lElapsed < kFlightEnd) {
            const float fp = (lElapsed - kRiseEnd) / (kFlightEnd - kRiseEnd);
            const float rp = 1.0f - fp;
            liftA = rp * 30.0f;
            liftB = rp * 30.0f;
        }
        curPosA.y -= liftA;
        curPosB.y -= liftB;

        // --- Midpoint heart glow (phases 0-3, before landing completes) ---
        if (lElapsed < kLandingEnd && heartGlowTex_.getSize().x > 0) {
            float glowScale, glowAlpha;
            if (lElapsed < kRevealEnd) {
                const float rp = lElapsed / kRevealEnd;
                glowScale = 0.4f + rp * 1.4f;
                glowAlpha = rp * 0.55f;
            } else if (lElapsed < kFlightEnd) {
                const float pulse = 1.0f + std::sin(lElapsed * 4.5f) * 0.25f;
                glowScale = 1.8f * pulse;
                glowAlpha = 0.55f;
            } else {
                const float lp = (lElapsed - kFlightEnd) / (kLandingEnd - kFlightEnd);
                glowScale = 1.8f + lp * 1.2f;
                glowAlpha = 0.55f * (1.0f - lp);
            }
            const auto gsz = sf::Vector2f(heartGlowTex_.getSize());
            sf::Sprite glowSpr(heartGlowTex_);
            glowSpr.setOrigin({gsz.x * 0.5f, gsz.y * 0.5f});
            glowSpr.setPosition(loversMidpoint_);
            glowSpr.setScale({glowScale, glowScale});
            glowSpr.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(glowAlpha * 255.0f)));
            window_.draw(glowSpr);
        }

        // --- Ribbon trails (Flight phase only) ---
        if (lElapsed >= kRiseEnd && lElapsed < kFlightEnd && loversArcPathA_.size() >= 36) {
            const float fp = (lElapsed - kRiseEnd) / (kFlightEnd - kRiseEnd);
            const float eased = fp < 0.5f
                ? 2.0f * fp * fp
                : 1.0f - std::pow(-2.0f * fp + 2.0f, 2.0f) / 2.0f;
            const int trailEnd = std::clamp(static_cast<int>(eased * 35.0f), 0, 35);
            const int trailStart = std::max(0, trailEnd - 14);
            const int numPts = trailEnd - trailStart + 1;
            if (numPts >= 2) {
                // Ribbon A (pink)
                sf::VertexArray ribbonA(sf::PrimitiveType::TriangleStrip, numPts * 2);
                for (int i = 0; i < numPts; ++i) {
                    const int pathIdx = trailStart + i;
                    const float t = static_cast<float>(i) / static_cast<float>(numPts - 1);
                    const float alpha = t * 0.45f;
                    sf::Vector2f perp;
                    if (pathIdx < 35) {
                        const float dx = loversArcPathA_[pathIdx + 1].x - loversArcPathA_[pathIdx].x;
                        const float dy = loversArcPathA_[pathIdx + 1].y - loversArcPathA_[pathIdx].y;
                        const float len = std::sqrt(dx * dx + dy * dy);
                        if (len > 0.001f) { perp = {-dy / len * 3.0f, dx / len * 3.0f}; }
                    }
                    ribbonA[i * 2].position = {loversArcPathA_[pathIdx].x + perp.x,
                                                loversArcPathA_[pathIdx].y + perp.y};
                    ribbonA[i * 2].color = sf::Color(255, 145, 175, static_cast<std::uint8_t>(alpha * 255.0f));
                    ribbonA[i * 2 + 1].position = {loversArcPathA_[pathIdx].x - perp.x,
                                                    loversArcPathA_[pathIdx].y - perp.y};
                    ribbonA[i * 2 + 1].color = sf::Color(255, 145, 175, static_cast<std::uint8_t>(alpha * 255.0f));
                }
                window_.draw(ribbonA);

                // Ribbon B (lighter pink)
                sf::VertexArray ribbonB(sf::PrimitiveType::TriangleStrip, numPts * 2);
                for (int i = 0; i < numPts; ++i) {
                    const int pathIdx = trailStart + i;
                    const float t = static_cast<float>(i) / static_cast<float>(numPts - 1);
                    const float alpha = t * 0.45f;
                    sf::Vector2f perp;
                    if (pathIdx < 35) {
                        const float dx = loversArcPathB_[pathIdx + 1].x - loversArcPathB_[pathIdx].x;
                        const float dy = loversArcPathB_[pathIdx + 1].y - loversArcPathB_[pathIdx].y;
                        const float len = std::sqrt(dx * dx + dy * dy);
                        if (len > 0.001f) { perp = {-dy / len * 3.0f, dx / len * 3.0f}; }
                    }
                    ribbonB[i * 2].position = {loversArcPathB_[pathIdx].x + perp.x,
                                                loversArcPathB_[pathIdx].y + perp.y};
                    ribbonB[i * 2].color = sf::Color(255, 185, 200, static_cast<std::uint8_t>(alpha * 255.0f));
                    ribbonB[i * 2 + 1].position = {loversArcPathB_[pathIdx].x - perp.x,
                                                    loversArcPathB_[pathIdx].y - perp.y};
                    ribbonB[i * 2 + 1].color = sf::Color(255, 185, 200, static_cast<std::uint8_t>(alpha * 255.0f));
                }
                window_.draw(ribbonB);
            }
        }

        // --- Flying pieces with glow (phases 0-3) ---
        if (lElapsed < kSettleEnd) {
            const float pieceR = 7.5f;
            const float shrinkLanding = (lElapsed >= kFlightEnd && lElapsed < kLandingEnd)
                ? 1.0f + std::sin((lElapsed - kFlightEnd) / (kLandingEnd - kFlightEnd) * 3.14159265f) * 0.15f
                : 1.0f;
            const float landingSettle = (lElapsed >= kLandingEnd && lElapsed < kSettleEnd)
                ? 1.0f + 0.15f * (1.0f - (lElapsed - kLandingEnd) / (kSettleEnd - kLandingEnd))
                : 1.0f;
            const float curR = pieceR * shrinkLanding * landingSettle;

            // Glow disc behind piece A
            sf::CircleShape glowA(curR + 4.0f, 20);
            glowA.setOrigin({curR + 4.0f, curR + 4.0f});
            glowA.setPosition(curPosA);
            glowA.setFillColor(sf::Color(255, 155, 180, 55));
            window_.draw(glowA);

            // Piece A
            sf::CircleShape pieceA(curR, 16);
            pieceA.setOrigin({curR, curR});
            pieceA.setPosition(curPosA);
            pieceA.setFillColor(colorA);
            pieceA.setOutlineThickness(1.2f);
            pieceA.setOutlineColor(sf::Color(185, 120, 145, 90));
            window_.draw(pieceA);

            // Glow disc behind piece B
            sf::CircleShape glowB(curR + 4.0f, 20);
            glowB.setOrigin({curR + 4.0f, curR + 4.0f});
            glowB.setPosition(curPosB);
            glowB.setFillColor(sf::Color(255, 155, 180, 55));
            window_.draw(glowB);

            // Piece B
            sf::CircleShape pieceB(curR, 16);
            pieceB.setOrigin({curR, curR});
            pieceB.setPosition(curPosB);
            pieceB.setFillColor(colorB);
            pieceB.setOutlineThickness(1.2f);
            pieceB.setOutlineColor(sf::Color(185, 120, 145, 90));
            window_.draw(pieceB);
        }

        // --- Heart burst at midpoint (Landing phase) ---
        if (lElapsed >= kFlightEnd && lElapsed < kSettleEnd && heartBurstTex_.getSize().x > 0) {
            float burstScale, burstAlpha;
            if (lElapsed < kLandingEnd) {
                const float lp = (lElapsed - kFlightEnd) / (kLandingEnd - kFlightEnd);
                burstScale = 0.6f + lp * 2.4f;
                burstAlpha = std::sin(lp * 3.14159265f) * 0.85f;
            } else {
                const float sp = (lElapsed - kLandingEnd) / (kSettleEnd - kLandingEnd);
                burstScale = 3.0f + sp * 0.6f;
                burstAlpha = (1.0f - sp) * 0.45f;
            }
            const auto bsz = sf::Vector2f(heartBurstTex_.getSize());
            sf::Sprite burstSpr(heartBurstTex_);
            burstSpr.setOrigin({bsz.x * 0.5f, bsz.y * 0.5f});
            burstSpr.setPosition(loversMidpoint_);
            burstSpr.setScale({burstScale, burstScale});
            burstSpr.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(burstAlpha * 255.0f)));
            window_.draw(burstSpr);
        }

        // --- Floating "命运交织" text ---
        if (loversFloatingTextAlpha_ > 0.01f && lElapsed < kSettleEnd && fontLoaded_) {
            const float textY = loversMidpoint_.y - 58.0f
                - (lElapsed >= kFlightEnd ? (lElapsed - kFlightEnd) * 15.0f : 0.0f);
            const float alpha = loversFloatingTextAlpha_;
            const auto baseAlpha = static_cast<std::uint8_t>(alpha * 255.0f);

            // Render at native pixel size then scale up for crisp blocky pixels
            constexpr float kPixelScale = 2.0f;
            constexpr unsigned kBaseSize = 13;
            const std::string fateStr = "命运交织";
            const auto fateUtf8 = sf::String::fromUtf8(fateStr.begin(), fateStr.end());

            // Helper to create and position a text layer
            auto makeLayer = [&](sf::Color color) {
                sf::Text t(uiFont_, fateUtf8, kBaseSize);
                t.setScale({kPixelScale, kPixelScale});
                t.setFillColor(color);
                return t;
            };

            // Pre-compute origin from a template
            sf::Text tmpl(uiFont_, fateUtf8, kBaseSize);
            tmpl.setScale({kPixelScale, kPixelScale});
            const auto tb = tmpl.getLocalBounds();
            const sf::Vector2f origin = {tb.size.x * 0.5f, tb.size.y * 0.5f};
            const sf::Vector2f center = {loversMidpoint_.x, textY};

            // Pixel offsets (in pre-scaled units)
            const sf::Vector2f shadowOff(3.0f, 3.0f);
            const sf::Vector2f midOff(2.0f, 2.0f);
            const sf::Vector2f hiOff(-1.0f, -1.0f);

            // Layer 4: Deep shadow (outermost)
            auto shadow = makeLayer(sf::Color(38, 6, 14, baseAlpha));
            shadow.setOrigin(origin);
            shadow.setPosition(center + shadowOff);
            window_.draw(shadow);

            // Layer 3: Mid-shadow / outline weight
            auto midShadow = makeLayer(sf::Color(130, 20, 45, baseAlpha));
            midShadow.setOrigin(origin);
            midShadow.setPosition(center + midOff);
            window_.draw(midShadow);

            // Layer 2: Main body — vibrant gradient core
            const auto mainColor = sf::Color(255, 78, 110, baseAlpha);
            auto mainText = makeLayer(mainColor);
            mainText.setOrigin(origin);
            mainText.setPosition(center);
            window_.draw(mainText);

            // Layer 1: Pixel highlight (top-left bevel)
            const auto hiAlpha = static_cast<std::uint8_t>(alpha * 200.0f);
            auto highlight = makeLayer(sf::Color(255, 210, 225, hiAlpha));
            highlight.setOrigin(origin);
            highlight.setPosition(center + hiOff);
            window_.draw(highlight);

            // Tiny decorative pixel hearts flanking the text
            if (heartSmallTex_.getSize().x > 0 && alpha > 0.5f) {
                const auto hsz = sf::Vector2f(heartSmallTex_.getSize());
                const float flankX = tb.size.x * 0.5f * kPixelScale + 22.0f;
                const float flankPulse = 1.0f + std::sin(lElapsed * 3.5f) * 0.3f;
                const auto flankAlpha = static_cast<std::uint8_t>(alpha * 160.0f);

                sf::Sprite leftHeart(heartSmallTex_);
                leftHeart.setOrigin({hsz.x * 0.5f, hsz.y * 0.5f});
                leftHeart.setPosition({center.x - flankX, center.y});
                leftHeart.setScale({flankPulse * 1.2f, flankPulse * 1.2f});
                leftHeart.setColor(sf::Color(255, 160, 180, flankAlpha));
                window_.draw(leftHeart);

                sf::Sprite rightHeart(heartSmallTex_);
                rightHeart.setOrigin({hsz.x * 0.5f, hsz.y * 0.5f});
                rightHeart.setPosition({center.x + flankX, center.y});
                rightHeart.setScale({flankPulse * 1.2f, flankPulse * 1.2f});
                rightHeart.setColor(sf::Color(255, 160, 180, flankAlpha));
                window_.draw(rightHeart);
            }
        }

        // --- All heart particles ---
        if (!loversHeartParticles_.empty() && heartSmallTex_.getSize().x > 0) {
            const auto hsz = sf::Vector2f(heartSmallTex_.getSize());
            for (const auto& hp : loversHeartParticles_) {
                const float lifeRatio = 1.0f - hp.life / hp.maxLife;
                const auto alpha = static_cast<std::uint8_t>(lifeRatio * 210.0f);
                if (alpha == 0) continue;
                sf::Sprite hspr(heartSmallTex_);
                hspr.setOrigin({hsz.x * 0.5f, hsz.y * 0.5f});
                hspr.setPosition(hp.pos);
                hspr.setScale({hp.scale * (0.6f + lifeRatio * 0.4f), hp.scale * (0.6f + lifeRatio * 0.4f)});
                hspr.setRotation(sf::degrees(hp.rotation));
                hspr.setColor(sf::Color(255, 255, 255, alpha));
                window_.draw(hspr);
            }
        }
    }

    // World card animation rendering
    if (worldAnimPending_) {
        const float wElapsed = worldAnimClock_.getElapsedTime().asSeconds();
        constexpr float kDescentEnd = 0.55f;
        constexpr float kExpandEnd = 1.3f;
        constexpr float kConvergeEnd = 2.0f;
        constexpr float kSealEnd = 2.5f;
        constexpr float kDissipateEnd = 3.0f;

        // --- Amber vignette overlay ---
        if (amberVignetteAlpha_ > 0.003f) {
            const auto va = static_cast<std::uint8_t>(amberVignetteAlpha_ * 255.0f);
            sf::RectangleShape vignette;
            vignette.setPosition({0.0f, 0.0f});
            vignette.setSize({1280.0f, 820.0f});
            vignette.setFillColor(sf::Color(200, 135, 25, va));
            window_.draw(vignette);
        }

        // --- Concentric golden rings ---
        for (int i = 0; i < 5; ++i) {
            if (ringRadii_[i] < 1.5f || ringAlphas_[i] < 0.01f) continue;
            const float r = ringRadii_[i];
            const auto alpha = static_cast<std::uint8_t>(ringAlphas_[i] * 255.0f);
            // Outer glow ring (thicker, more transparent)
            sf::CircleShape glowRing(r + 3.0f, 48);
            glowRing.setOrigin({r + 3.0f, r + 3.0f});
            glowRing.setPosition(mandalaCenter_);
            glowRing.setFillColor(sf::Color::Transparent);
            glowRing.setOutlineThickness(3.5f);
            glowRing.setOutlineColor(sf::Color(255, 185, 40, static_cast<std::uint8_t>(alpha * 0.35f)));
            window_.draw(glowRing);
            // Main ring
            sf::CircleShape ring(r, 48);
            ring.setOrigin({r, r});
            ring.setPosition(mandalaCenter_);
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineThickness(2.0f);
            ring.setOutlineColor(sf::Color(235, 175, 35, alpha));
            window_.draw(ring);
            // Inner bright ring
            sf::CircleShape innerRing(r - 1.5f, 48);
            innerRing.setOrigin({r - 1.5f, r - 1.5f});
            innerRing.setPosition(mandalaCenter_);
            innerRing.setFillColor(sf::Color::Transparent);
            innerRing.setOutlineThickness(1.0f);
            innerRing.setOutlineColor(sf::Color(255, 215, 80, static_cast<std::uint8_t>(alpha * 0.5f)));
            window_.draw(innerRing);
            // Decorative dots along the ring
            if (r > 20.0f && ringAlphas_[i] > 0.3f) {
                constexpr int kDots = 16;
                for (int d = 0; d < kDots; ++d) {
                    const float da = static_cast<float>(d) / static_cast<float>(kDots) * 2.0f * 3.14159265f;
                    const float dx = mandalaCenter_.x + std::cos(da) * r;
                    const float dy = mandalaCenter_.y + std::sin(da) * r;
                    sf::RectangleShape dot;
                    dot.setPosition({std::round(dx - 1.5f), std::round(dy - 1.5f)});
                    dot.setSize({3.0f, 3.0f});
                    dot.setFillColor(sf::Color(255, 230, 140, static_cast<std::uint8_t>(alpha * 0.6f)));
                    window_.draw(dot);
                }
            }
        }

        // --- Mandala gear sprite ---
        if (mandalaAlpha_ > 0.01f && mandalaTex_.getSize().x > 0) {
            const auto msz = sf::Vector2f(mandalaTex_.getSize());
            sf::Sprite mandala(mandalaTex_);
            mandala.setOrigin({msz.x * 0.5f, msz.y * 0.5f});
            mandala.setPosition(mandalaCenter_);
            mandala.setScale({mandalaScale_, mandalaScale_});
            mandala.setRotation(sf::degrees(mandalaRotation_ * 180.0f / 3.14159265f));
            mandala.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(mandalaAlpha_ * 255.0f)));
            window_.draw(mandala);

            // Subtle additive glow duplicate behind mandala
            if (mandalaScale_ > 0.5f) {
                sf::Sprite mandalaGlow(mandalaTex_);
                mandalaGlow.setOrigin({msz.x * 0.5f, msz.y * 0.5f});
                mandalaGlow.setPosition(mandalaCenter_);
                mandalaGlow.setScale({mandalaScale_ * 1.2f, mandalaScale_ * 1.2f});
                mandalaGlow.setRotation(sf::degrees(mandalaRotation_ * 180.0f / 3.14159265f));
                mandalaGlow.setColor(sf::Color(255, 200, 60, static_cast<std::uint8_t>(mandalaAlpha_ * 100.0f)));
                window_.draw(mandalaGlow);
            }
        }

        // --- "周而复始" floating text (phases 0-2) ---
        if (wElapsed < kConvergeEnd && fontLoaded_ && mandalaAlpha_ > 0.3f) {
            float textAlpha;
            if (wElapsed < kDescentEnd)
                textAlpha = wElapsed / kDescentEnd * 0.8f;
            else if (wElapsed < kExpandEnd)
                textAlpha = 0.8f;
            else
                textAlpha = 0.8f * (1.0f - (wElapsed - kExpandEnd) / (kConvergeEnd - kExpandEnd));
            const float textY = mandalaCenter_.y - 64.0f - mandalaScale_ * 20.0f;
            const std::string wsStr = "周而复始";
            const auto wsUtf8 = sf::String::fromUtf8(wsStr.begin(), wsStr.end());
            // Shadow
            sf::Text wsShadow(uiFont_, wsUtf8, 13);
            wsShadow.setScale({2.0f, 2.0f});
            wsShadow.setFillColor(sf::Color(40, 20, 5, static_cast<std::uint8_t>(textAlpha * 200.0f)));
            auto wb = wsShadow.getLocalBounds();
            wsShadow.setOrigin({wb.size.x * 0.5f, wb.size.y * 0.5f});
            wsShadow.setPosition({mandalaCenter_.x + 2.0f, textY + 2.0f});
            window_.draw(wsShadow);
            // Main
            sf::Text wsMain(uiFont_, wsUtf8, 13);
            wsMain.setScale({2.0f, 2.0f});
            wsMain.setFillColor(sf::Color(255, 200, 55, static_cast<std::uint8_t>(textAlpha * 255.0f)));
            wsMain.setOrigin({wb.size.x * 0.5f, wb.size.y * 0.5f});
            wsMain.setPosition({mandalaCenter_.x, textY});
            window_.draw(wsMain);
            // Highlight
            sf::Text wsHi(uiFont_, wsUtf8, 13);
            wsHi.setScale({2.0f, 2.0f});
            wsHi.setFillColor(sf::Color(255, 240, 180, static_cast<std::uint8_t>(textAlpha * 140.0f)));
            wsHi.setOrigin({wb.size.x * 0.5f, wb.size.y * 0.5f});
            wsHi.setPosition({mandalaCenter_.x - 1.0f, textY - 1.0f});
            window_.draw(wsHi);
        }

        // --- Seal cross flash (phase 3) ---
        if (sealFlashAlpha_ > 0.01f) {
            const auto fa = static_cast<std::uint8_t>(sealFlashAlpha_ * 255.0f);
            const float sx = sealTarget_.x, sy = sealTarget_.y;
            // Horizontal blade
            sf::RectangleShape hBlade;
            hBlade.setPosition({sx - 60.0f, sy - 4.0f});
            hBlade.setSize({120.0f, 8.0f});
            hBlade.setFillColor(sf::Color(255, 240, 180, fa));
            window_.draw(hBlade);
            // Vertical blade
            sf::RectangleShape vBlade;
            vBlade.setPosition({sx - 4.0f, sy - 60.0f});
            vBlade.setSize({8.0f, 120.0f});
            vBlade.setFillColor(sf::Color(255, 240, 180, fa));
            window_.draw(vBlade);
            // Core diamond
            sf::RectangleShape diamond;
            diamond.setPosition({sx - 6.0f, sy - 6.0f});
            diamond.setSize({12.0f, 12.0f});
            diamond.setRotation(sf::degrees(45.0f));
            diamond.setFillColor(sf::Color(255, 255, 230, fa));
            window_.draw(diamond);
            // Outer ring burst
            sf::CircleShape burst(18.0f, 24);
            burst.setOrigin({18.0f, 18.0f});
            burst.setPosition(sealTarget_);
            burst.setFillColor(sf::Color::Transparent);
            burst.setOutlineThickness(2.5f);
            burst.setOutlineColor(sf::Color(255, 220, 60, static_cast<std::uint8_t>(fa * 0.6f)));
            window_.draw(burst);
        }

        // --- "无法悔棋" label at seal target (phase 3-4) ---
        if (wElapsed >= kConvergeEnd && wElapsed < kDissipateEnd && fontLoaded_ && worldSealTriggered_) {
            float labelAlpha;
            if (wElapsed < kSealEnd)
                labelAlpha = (wElapsed - kConvergeEnd) / (kSealEnd - kConvergeEnd);
            else
                labelAlpha = 1.0f - (wElapsed - kSealEnd) / (kDissipateEnd - kSealEnd);
            labelAlpha = std::clamp(labelAlpha, 0.0f, 1.0f) * 0.85f;
            if (labelAlpha > 0.02f) {
                const std::string sealStr = "无法悔棋";
                const auto sealUtf8 = sf::String::fromUtf8(sealStr.begin(), sealStr.end());
                const float sealTextY = sealTarget_.y + 30.0f;
                sf::Text sealText(uiFont_, sealUtf8, 11);
                sealText.setScale({2.0f, 2.0f});
                sealText.setFillColor(sf::Color(240, 160, 40, static_cast<std::uint8_t>(labelAlpha * 255.0f)));
                auto sb = sealText.getLocalBounds();
                sealText.setOrigin({sb.size.x * 0.5f, sb.size.y * 0.5f});
                sealText.setPosition({sealTarget_.x, sealTextY});
                window_.draw(sealText);
            }
        }

        // --- World particles (golden diamond sparks) ---
        if (!worldParticles_.empty() && goldSparkTex_.getSize().x > 0) {
            const auto gsz = sf::Vector2f(goldSparkTex_.getSize());
            for (const auto& wp : worldParticles_) {
                const float lifeRatio = 1.0f - wp.life / wp.maxLife;
                const auto alpha = static_cast<std::uint8_t>(lifeRatio * 220.0f);
                if (alpha == 0) continue;
                sf::Sprite sp(goldSparkTex_);
                sp.setOrigin({gsz.x * 0.5f, gsz.y * 0.5f});
                sp.setPosition(wp.pos);
                sp.setScale({wp.scale * (0.5f + lifeRatio * 0.5f), wp.scale * (0.5f + lifeRatio * 0.5f)});
                sp.setRotation(sf::degrees(wp.rotation));
                sp.setColor(sf::Color(255, 255, 255, alpha));
                window_.draw(sp);
            }
        }
    }

    // Strength card animation rendering (private: only drawer sees it)
    if (strengthAnimPending_ && (!networkMode_ || iAmPicker_)) {
        const sf::Vector2f piecePx = board_.cellToPixel(strengthProtectedPos_.x, strengthProtectedPos_.y);

        // Golden flash overlay at piece position
        if (strengthFlashAlpha_ > 0.01f) {
            const auto fa = static_cast<std::uint8_t>(strengthFlashAlpha_ * 255.0f);
            sf::CircleShape flash(18.0f, 24);
            flash.setOrigin({18.0f, 18.0f});
            flash.setPosition(piecePx);
            flash.setFillColor(sf::Color(255, 215, 60, fa));
            window_.draw(flash);
            // Core white-hot center
            sf::CircleShape core(8.0f, 16);
            core.setOrigin({8.0f, 8.0f});
            core.setPosition(piecePx);
            core.setFillColor(sf::Color(255, 255, 230, fa));
            window_.draw(core);
        }

        // Shockwave ring
        if (strengthShockwaveAlpha_ > 0.01f && strengthShockwaveRadius_ > 1.0f) {
            const auto sa = static_cast<std::uint8_t>(strengthShockwaveAlpha_ * 255.0f);
            const float sr = strengthShockwaveRadius_;
            // Outer glow
            sf::CircleShape waveOuter(sr + 4.0f, 36);
            waveOuter.setOrigin({sr + 4.0f, sr + 4.0f});
            waveOuter.setPosition(piecePx);
            waveOuter.setFillColor(sf::Color::Transparent);
            waveOuter.setOutlineThickness(4.0f);
            waveOuter.setOutlineColor(sf::Color(255, 185, 40, static_cast<std::uint8_t>(sa * 0.3f)));
            window_.draw(waveOuter);
            // Main ring
            sf::CircleShape wave(sr, 36);
            wave.setOrigin({sr, sr});
            wave.setPosition(piecePx);
            wave.setFillColor(sf::Color::Transparent);
            wave.setOutlineThickness(2.5f);
            wave.setOutlineColor(sf::Color(255, 205, 50, sa));
            window_.draw(wave);
            // Inner bright ring
            sf::CircleShape waveInner(sr - 2.0f, 36);
            waveInner.setOrigin({sr - 2.0f, sr - 2.0f});
            waveInner.setPosition(piecePx);
            waveInner.setFillColor(sf::Color::Transparent);
            waveInner.setOutlineThickness(1.0f);
            waveInner.setOutlineColor(sf::Color(255, 235, 120, static_cast<std::uint8_t>(sa * 0.5f)));
            window_.draw(waveInner);
        }

        // Strength rune sprite at piece position
        const float sElapsed = strengthAnimClock_.getElapsedTime().asSeconds();
        constexpr float kConvergeEndSR = 0.25f;
        constexpr float kInfuseEndSR = 0.55f;
        constexpr float kSettleEndSR = 1.0f;
        float runeAlpha = 0.0f;
        if (sElapsed >= kConvergeEndSR && sElapsed < kInfuseEndSR) {
            runeAlpha = (sElapsed - kConvergeEndSR) / (kInfuseEndSR - kConvergeEndSR);
        } else if (sElapsed >= kInfuseEndSR && sElapsed < kSettleEndSR) {
            runeAlpha = 1.0f - (sElapsed - kInfuseEndSR) / (kSettleEndSR - kInfuseEndSR);
        }
        if (runeAlpha > 0.01f && strengthRuneTex_.getSize().x > 0) {
            const auto rsz = sf::Vector2f(strengthRuneTex_.getSize());
            sf::Sprite rune(strengthRuneTex_);
            rune.setOrigin({rsz.x * 0.5f, rsz.y * 0.5f});
            rune.setPosition(piecePx);
            const float runeScale = 0.8f + runeAlpha * 0.4f;
            rune.setScale({runeScale, runeScale});
            rune.setRotation(sf::degrees(sElapsed * 120.0f));
            rune.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(runeAlpha * 220.0f)));
            window_.draw(rune);
        }

        // Strength particles
        if (!strengthParticles_.empty() && strengthSparkTex_.getSize().x > 0) {
            const auto ssz = sf::Vector2f(strengthSparkTex_.getSize());
            for (const auto& sp : strengthParticles_) {
                const float lifeRatio = 1.0f - sp.life / sp.maxLife;
                const auto alpha = static_cast<std::uint8_t>(lifeRatio * 210.0f);
                if (alpha == 0) continue;
                sf::Sprite spr(strengthSparkTex_);
                spr.setOrigin({ssz.x * 0.5f, ssz.y * 0.5f});
                spr.setPosition(sp.pos);
                spr.setScale({sp.scale * (0.4f + lifeRatio * 0.6f), sp.scale * (0.4f + lifeRatio * 0.6f)});
                spr.setColor(sf::Color(255, 255, 255, alpha));
                window_.draw(spr);
            }
        }
    }

    // Persistent indicator (private: only drawer sees the empowered piece glow)
    if (strengthProtectionRemaining_ > 0 && !strengthAnimPending_ &&
        strengthProtectedPos_.x >= 0 && strengthProtectedPos_.y >= 0 &&
        (!networkMode_ || iAmPicker_)) {
        const sf::Vector2f pp = board_.cellToPixel(strengthProtectedPos_.x, strengthProtectedPos_.y);
        const float orbitRadius = 16.0f;
        const float orbitTime = atmosphereClock_.getElapsedTime().asSeconds();

        // Golden pulsing outline
        const float pulse = 0.5f + 0.5f * std::sin(orbitTime * 3.0f);
        const auto glowAlpha = static_cast<std::uint8_t>((0.5f + pulse * 0.35f) * 255.0f);
        sf::CircleShape outline(orbitRadius + 1.0f, 24);
        outline.setOrigin({orbitRadius + 1.0f, orbitRadius + 1.0f});
        outline.setPosition(pp);
        outline.setFillColor(sf::Color::Transparent);
        outline.setOutlineThickness(2.5f);
        outline.setOutlineColor(sf::Color(255, 190, 35, glowAlpha));
        window_.draw(outline);

        // Inner dotted ring
        sf::CircleShape dots(orbitRadius - 2.0f, 24);
        dots.setOrigin({orbitRadius - 2.0f, orbitRadius - 2.0f});
        dots.setPosition(pp);
        dots.setFillColor(sf::Color::Transparent);
        dots.setOutlineThickness(1.0f);
        dots.setOutlineColor(sf::Color(255, 220, 80, static_cast<std::uint8_t>(glowAlpha * 0.5f)));
        window_.draw(dots);

        // Orbiting motes (one per remaining turn)
        for (int i = 0; i < strengthProtectionRemaining_; ++i) {
            const float phase = static_cast<float>(i) / static_cast<float>(strengthProtectionRemaining_);
            const float angle = orbitTime * 1.8f + phase * 2.0f * 3.14159265f;
            const float mx = pp.x + std::cos(angle) * orbitRadius;
            const float my = pp.y + std::sin(angle) * orbitRadius;
            const float motePulse = 0.6f + 0.4f * std::sin(orbitTime * 4.0f + phase * 6.0f);
            const auto moteAlpha = static_cast<std::uint8_t>(motePulse * 200.0f);
            sf::CircleShape mote(2.5f, 6);
            mote.setOrigin({2.5f, 2.5f});
            mote.setPosition({mx, my});
            mote.setFillColor(sf::Color(255, 220, 60, moteAlpha));
            window_.draw(mote);
            // Tiny glow halo
            sf::CircleShape moteGlow(4.5f, 8);
            moteGlow.setOrigin({4.5f, 4.5f});
            moteGlow.setPosition({mx, my});
            moteGlow.setFillColor(sf::Color(255, 200, 40, static_cast<std::uint8_t>(moteAlpha * 0.3f)));
            window_.draw(moteGlow);
        }
    }

    // Empress card animation rendering
    if (empressAnimPending_) {
        const float eElapsed = empressAnimClock_.getElapsedTime().asSeconds();
        constexpr float kBloomEndER = 0.95f;

        const sf::Vector2f srcPx = board_.cellToPixel(empressSourcePiece_.x, empressSourcePiece_.y);
        const sf::Vector2f tgtPx = board_.cellToPixel(empressTargetCell_.x, empressTargetCell_.y);

        // --- Vine: bezier curve from source to target ---
        if (empressVineProgress_ > 0.005f) {
            const float vp = empressVineProgress_;
            // Control point: offset perpendicular to line with upward bias
            const float dxV = tgtPx.x - srcPx.x;
            const float dyV = tgtPx.y - srcPx.y;
            const float distV = std::sqrt(dxV * dxV + dyV * dyV);
            const sf::Vector2f mid = {(srcPx.x + tgtPx.x) * 0.5f, (srcPx.y + tgtPx.y) * 0.5f};
            float perpVX = -dyV;
            float perpVY = dxV;
            if (perpVY > 0) { perpVX = -perpVX; perpVY = -perpVY; }
            const float perpLenV = std::sqrt(perpVX * perpVX + perpVY * perpVY);
            const float offsetV = std::max(18.0f, distV * 0.28f);
            const sf::Vector2f cp = {mid.x + (perpVX / perpLenV) * offsetV,
                                     mid.y + (perpVY / perpLenV) * offsetV};

            // Draw vine as chain of small segments (LineStrip) + overlapping circles for thickness
            constexpr int kVineSegs = 40;
            const int activeSegs = static_cast<int>(static_cast<float>(kVineSegs) * vp);
            std::vector<sf::Vector2f> vinePts;
            vinePts.reserve(activeSegs + 1);
            for (int i = 0; i <= activeSegs; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(kVineSegs);
                const float u = 1.0f - t;
                vinePts.push_back({u*u*srcPx.x + 2*u*t*cp.x + t*t*tgtPx.x,
                                   u*u*srcPx.y + 2*u*t*cp.y + t*t*tgtPx.y});
            }

            // Main vine body: thick green line with glow
            if (vinePts.size() >= 2) {
                // Outer glow
                sf::VertexArray vineGlow(sf::PrimitiveType::TriangleStrip, vinePts.size() * 2);
                const float glowHalfW = 5.0f;
                for (std::size_t i = 0; i < vinePts.size(); ++i) {
                    sf::Vector2f dir;
                    if (i == 0) dir = vinePts[1] - vinePts[0];
                    else if (i == vinePts.size() - 1) dir = vinePts[i] - vinePts[i-1];
                    else dir = vinePts[i+1] - vinePts[i-1];
                    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                    if (len < 0.001f) dir = {1.0f, 0.0f}; else dir /= len;
                    const sf::Vector2f perp = {-dir.y, dir.x};
                    const float thick = glowHalfW * (1.0f - 0.3f * std::abs(static_cast<float>(i) / static_cast<float>(vinePts.size()) - 0.5f) * 2.0f);
                    vineGlow[i*2].position = vinePts[i] + perp * thick;
                    vineGlow[i*2].color = sf::Color(80, 170, 50, 60);
                    vineGlow[i*2+1].position = vinePts[i] - perp * thick;
                    vineGlow[i*2+1].color = sf::Color(80, 170, 50, 60);
                }
                window_.draw(vineGlow);

                // Core vine
                sf::VertexArray vineCore(sf::PrimitiveType::TriangleStrip, vinePts.size() * 2);
                const float coreHalfW = 2.5f;
                for (std::size_t i = 0; i < vinePts.size(); ++i) {
                    sf::Vector2f dir;
                    if (i == 0) dir = vinePts[1] - vinePts[0];
                    else if (i == vinePts.size() - 1) dir = vinePts[i] - vinePts[i-1];
                    else dir = vinePts[i+1] - vinePts[i-1];
                    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                    if (len < 0.001f) dir = {1.0f, 0.0f}; else dir /= len;
                    const sf::Vector2f perp = {-dir.y, dir.x};
                    vineCore[i*2].position = vinePts[i] + perp * coreHalfW;
                    vineCore[i*2].color = sf::Color(100, 195, 60, 200);
                    vineCore[i*2+1].position = vinePts[i] - perp * coreHalfW;
                    vineCore[i*2+1].color = sf::Color(100, 195, 60, 200);
                }
                window_.draw(vineCore);

                // Vine tip: bright green bud
                const sf::Vector2f tip = vinePts.back();
                sf::CircleShape tipGlow(4.0f, 8);
                tipGlow.setOrigin({4.0f, 4.0f});
                tipGlow.setPosition(tip);
                tipGlow.setFillColor(sf::Color(160, 230, 80, 180));
                window_.draw(tipGlow);
            }
        }

        // --- Bloom: layered petals at target cell ---
        if (empressBloomPhase_ > 0.01f && petalTex_.getSize().x > 0) {
            const float bp = empressBloomPhase_;
            const auto psz = sf::Vector2f(petalTex_.getSize());

            // Bloom glow behind petals
            if (bloomGlowTex_.getSize().x > 0) {
                const float glowScale = 0.6f + bp * 1.2f;
                const auto glowAlpha = static_cast<std::uint8_t>(bp * 180.0f);
                sf::Sprite glowSpr(bloomGlowTex_);
                glowSpr.setOrigin({32.0f, 32.0f});
                glowSpr.setPosition(tgtPx);
                glowSpr.setScale({glowScale, glowScale});
                glowSpr.setColor(sf::Color(255, 255, 255, glowAlpha));
                window_.draw(glowSpr);
            }

            // 3 concentric rings of petals
            const struct { int count; float radius; float scale; int r, g, b; } rings[] = {
                {5,  8.0f,  0.65f, 255, 200, 130}, // inner: pink-white
                {8,  14.0f, 0.85f, 245, 140, 140}, // middle: rose-pink
                {12, 20.0f, 0.75f, 230, 100, 130}, // outer: deeper rose
            };
            for (const auto& ring : rings) {
                for (int i = 0; i < ring.count; ++i) {
                    const float angle = static_cast<float>(i) / static_cast<float>(ring.count) * 2.0f * 3.14159265f;
                    const float openAngle = angle + bp * 0.5f; // petals rotate open as bloom progresses
                    const float px = tgtPx.x + std::cos(openAngle) * ring.radius * bp;
                    const float py = tgtPx.y + std::sin(openAngle) * ring.radius * bp;
                    const float petalAlpha = std::min(1.0f, bp * 2.0f) * (0.7f + 0.3f * bp);
                    sf::Sprite petalSpr(petalTex_);
                    petalSpr.setOrigin({psz.x * 0.5f, psz.y * 0.75f}); // origin near base
                    petalSpr.setPosition({px, py});
                    petalSpr.setScale({ring.scale, ring.scale});
                    petalSpr.setRotation(sf::degrees(openAngle * 180.0f / 3.14159265f + 90.0f));
                    petalSpr.setColor(sf::Color(static_cast<std::uint8_t>(ring.r),
                                                static_cast<std::uint8_t>(ring.g),
                                                static_cast<std::uint8_t>(ring.b),
                                                static_cast<std::uint8_t>(petalAlpha * 255.0f)));
                    window_.draw(petalSpr);
                }
            }
        }

        // --- "生机盎然" floating text ---
        if (empressBloomPhase_ > 0.2f && fontLoaded_) {
            float textAlpha;
            if (empressBloomPhase_ < 1.0f)
                textAlpha = std::min(1.0f, (empressBloomPhase_ - 0.2f) / 0.5f) * 0.78f;
            else
                textAlpha = 0.78f * std::max(0.0f, 1.0f - (eElapsed - kBloomEndER) / 0.4f);
            if (textAlpha > 0.02f) {
                const float textY = tgtPx.y - 32.0f;
                const std::string bloomStr = "生机盎然";
                const auto bloomUtf8 = sf::String::fromUtf8(bloomStr.begin(), bloomStr.end());
                // Shadow
                sf::Text bShadow(uiFont_, bloomUtf8, 12);
                bShadow.setScale({2.0f, 2.0f});
                bShadow.setFillColor(sf::Color(20, 50, 15, static_cast<std::uint8_t>(textAlpha * 160.0f)));
                auto bb = bShadow.getLocalBounds();
                bShadow.setOrigin({bb.size.x * 0.5f, bb.size.y * 0.5f});
                bShadow.setPosition({tgtPx.x + 2.0f, textY + 2.0f});
                window_.draw(bShadow);
                // Main
                sf::Text bMain(uiFont_, bloomUtf8, 12);
                bMain.setScale({2.0f, 2.0f});
                bMain.setFillColor(sf::Color(180, 230, 100, static_cast<std::uint8_t>(textAlpha * 255.0f)));
                bMain.setOrigin({bb.size.x * 0.5f, bb.size.y * 0.5f});
                bMain.setPosition({tgtPx.x, textY});
                window_.draw(bMain);
                // Highlight
                sf::Text bHi(uiFont_, bloomUtf8, 12);
                bHi.setScale({2.0f, 2.0f});
                bHi.setFillColor(sf::Color(230, 255, 180, static_cast<std::uint8_t>(textAlpha * 140.0f)));
                bHi.setOrigin({bb.size.x * 0.5f, bb.size.y * 0.5f});
                bHi.setPosition({tgtPx.x - 1.0f, textY - 1.0f});
                window_.draw(bHi);
            }
        }

        // --- Flying petals ---
        if (!empressPetals_.empty() && petalTex_.getSize().x > 0) {
            const auto psz = sf::Vector2f(petalTex_.getSize());
            for (const auto& p : empressPetals_) {
                const float lifeRatio = 1.0f - p.life / p.maxLife;
                const auto alpha = static_cast<std::uint8_t>(lifeRatio * 200.0f);
                if (alpha == 0) continue;
                // Color by type
                sf::Color tint;
                switch (p.colorType) {
                case 0: tint = sf::Color(255, 180, 170, alpha); break; // soft pink
                case 1: tint = sf::Color(240, 120, 130, alpha); break; // rose
                case 2: tint = sf::Color(255, 220, 120, alpha); break; // warm gold
                default: tint = sf::Color(255, 180, 170, alpha); break;
                }
                sf::Sprite spr(petalTex_);
                spr.setOrigin({psz.x * 0.5f, psz.y * 0.5f});
                spr.setPosition(p.pos);
                spr.setScale({p.scale * (0.5f + lifeRatio * 0.5f), p.scale * (0.5f + lifeRatio * 0.5f)});
                spr.setRotation(sf::degrees(p.rotation));
                spr.setColor(tint);
                window_.draw(spr);
            }
        }

        // --- Fireflies ---
        for (const auto& ff : empressFireflies_) {
            const float lifeRatio = 1.0f - ff.life / ff.maxLife;
            const float pulse = 0.3f + 0.7f * std::abs(std::sin(ff.glowPhase + eElapsed * 6.0f));
            const auto alpha = static_cast<std::uint8_t>(lifeRatio * pulse * 200.0f);
            if (alpha < 8) continue;
            // Outer glow
            sf::CircleShape ffGlow(ff.baseRadius * 1.8f, 8);
            ffGlow.setOrigin({ff.baseRadius * 1.8f, ff.baseRadius * 1.8f});
            ffGlow.setPosition(ff.pos);
            ffGlow.setFillColor(sf::Color(200, 240, 100, static_cast<std::uint8_t>(alpha * 0.35f)));
            window_.draw(ffGlow);
            // Core
            sf::CircleShape ffCore(ff.baseRadius * 0.6f, 6);
            ffCore.setOrigin({ff.baseRadius * 0.6f, ff.baseRadius * 0.6f});
            ffCore.setPosition(ff.pos);
            ffCore.setFillColor(sf::Color(220, 255, 140, alpha));
            window_.draw(ffCore);
        }
    }

    // High Priestess card animation rendering
    if (highPriestessAnimPending_) {
        const sf::Vector2f centerPx = board_.cellToPixel(hpCrossCenter_.x, hpCrossCenter_.y);

        // --- Pillars: two luminous columns flanking the cross center ---
        if (hpPillarAlpha_ > 0.01f && pillarGlowTex_.getSize().x > 0) {
            const float pillarGap = 22.0f;
            const float pillarHeight = 160.0f;
            const auto psz = sf::Vector2f(pillarGlowTex_.getSize());
            const auto pa = static_cast<std::uint8_t>(hpPillarAlpha_ * 255.0f);

            for (int side = -1; side <= 1; side += 2) {
                const float px = centerPx.x + static_cast<float>(side) * pillarGap;
                const float py = centerPx.y - pillarHeight * 0.55f + hpPillarYOffset_;

                // Outer glow
                sf::Sprite pillarGlow(pillarGlowTex_);
                pillarGlow.setOrigin({psz.x * 0.5f, psz.y * 0.5f});
                pillarGlow.setPosition({px, py});
                pillarGlow.setScale({1.5f, pillarHeight / psz.y * 1.1f});
                pillarGlow.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(pa * 0.45f)));
                window_.draw(pillarGlow);

                // Core pillar
                sf::Sprite pillarCore(pillarGlowTex_);
                pillarCore.setOrigin({psz.x * 0.5f, psz.y * 0.5f});
                pillarCore.setPosition({px, py});
                pillarCore.setScale({0.7f, pillarHeight / psz.y});
                pillarCore.setColor(sf::Color(255, 255, 255, pa));
                window_.draw(pillarCore);

                // Pillar base highlight
                const float baseY = py + pillarHeight * 0.45f;
                sf::CircleShape baseGlow(8.0f, 12);
                baseGlow.setOrigin({8.0f, 8.0f});
                baseGlow.setPosition({px, baseY});
                baseGlow.setFillColor(sf::Color(200, 230, 255, static_cast<std::uint8_t>(pa * 0.6f)));
                window_.draw(baseGlow);
            }
        }

        // --- Cross beams: horizontal + vertical energy waves ---
        if (hpBeamProgress_ > 0.005f) {
            const float bp = hpBeamProgress_;
            const auto beamAlpha = static_cast<std::uint8_t>(std::min(1.0f, bp * 1.5f) * 210.0f);
            // Beam extends from center outward to board edges
            const float bLeft   = board_.cellToPixel(0, 0).x;
            const float bRight  = board_.cellToPixel(0, Board::kBoardSize - 1).x;
            const float bTop    = board_.cellToPixel(0, 0).y;
            const float bBottom = board_.cellToPixel(Board::kBoardSize - 1, 0).y;
            const float easedBp = 1.0f - (1.0f - bp) * (1.0f - bp);

            // Horizontal beam: center → left/right edges
            const float hLeft   = centerPx.x + (bLeft  - centerPx.x) * easedBp;
            const float hRight  = centerPx.x + (bRight - centerPx.x) * easedBp;
            const float hWidth  = hRight - hLeft;
            const float hy = centerPx.y;
            // Outer glow
            sf::RectangleShape hGlow;
            hGlow.setPosition({hLeft, hy - 7.0f});
            hGlow.setSize({hWidth, 14.0f});
            hGlow.setFillColor(sf::Color(160, 210, 255, static_cast<std::uint8_t>(beamAlpha * 0.4f)));
            window_.draw(hGlow);
            // Main beam
            sf::RectangleShape hMain;
            hMain.setPosition({hLeft, hy - 4.0f});
            hMain.setSize({hWidth, 8.0f});
            hMain.setFillColor(sf::Color(180, 220, 255, beamAlpha));
            window_.draw(hMain);
            // Core
            sf::RectangleShape hCore;
            hCore.setPosition({hLeft, hy - 2.0f});
            hCore.setSize({hWidth, 4.0f});
            hCore.setFillColor(sf::Color(220, 240, 255, beamAlpha));
            window_.draw(hCore);

            // Vertical beam: center → top/bottom edges
            const float vTop    = centerPx.y + (bTop    - centerPx.y) * easedBp;
            const float vBottom = centerPx.y + (bBottom - centerPx.y) * easedBp;
            const float vHeight = vBottom - vTop;
            const float vx = centerPx.x;
            sf::RectangleShape vGlow;
            vGlow.setPosition({vx - 7.0f, vTop});
            vGlow.setSize({14.0f, vHeight});
            vGlow.setFillColor(sf::Color(160, 210, 255, static_cast<std::uint8_t>(beamAlpha * 0.4f)));
            window_.draw(vGlow);
            sf::RectangleShape vMain;
            vMain.setPosition({vx - 4.0f, vTop});
            vMain.setSize({8.0f, vHeight});
            vMain.setFillColor(sf::Color(180, 220, 255, beamAlpha));
            window_.draw(vMain);
            sf::RectangleShape vCore;
            vCore.setPosition({vx - 2.0f, vTop});
            vCore.setSize({4.0f, vHeight});
            vCore.setFillColor(sf::Color(220, 240, 255, beamAlpha));
            window_.draw(vCore);

            // Center glow burst
            const float burstR = 10.0f + bp * 6.0f;
            sf::CircleShape burst(burstR, 16);
            burst.setOrigin({burstR, burstR});
            burst.setPosition(centerPx);
            burst.setFillColor(sf::Color(240, 245, 255, static_cast<std::uint8_t>(bp * 200.0f)));
            window_.draw(burst);
        }

        // --- Dissolving pieces: white glow overlay ---
        for (const auto& dp : hpDissolvingPieces_) {
            const float dAlpha = (1.0f - dp.dissolveProgress) * 0.8f;
            if (dAlpha < 0.02f) continue;
            const auto da = static_cast<std::uint8_t>(dAlpha * 255.0f);
            // Glowing circle at piece position
            sf::CircleShape glow(8.0f, 12);
            glow.setOrigin({8.0f, 8.0f});
            glow.setPosition(dp.pixelPos);
            glow.setFillColor(sf::Color(220, 235, 255, da));
            window_.draw(glow);
            // White core
            sf::CircleShape core(4.0f, 8);
            core.setOrigin({4.0f, 4.0f});
            core.setPosition(dp.pixelPos);
            core.setFillColor(sf::Color(240, 245, 255, da));
            window_.draw(core);
        }

        // --- "圣域结界" floating text ---
        if (hpBeamProgress_ > 0.15f && fontLoaded_) {
            const float textAlpha = hpBeamProgress_ > 0.9f
                ? 0.72f * (1.0f - (hpBeamProgress_ - 0.9f) / 0.1f)
                : 0.72f * hpBeamProgress_;
            if (textAlpha > 0.03f) {
                const float textY = centerPx.y - 36.0f;
                const std::string hpStr = "圣域结界";
                const auto hpUtf8 = sf::String::fromUtf8(hpStr.begin(), hpStr.end());
                // Shadow
                sf::Text hpShadow(uiFont_, hpUtf8, 12);
                hpShadow.setScale({2.0f, 2.0f});
                hpShadow.setFillColor(sf::Color(20, 30, 60, static_cast<std::uint8_t>(textAlpha * 150.0f)));
                auto hb = hpShadow.getLocalBounds();
                hpShadow.setOrigin({hb.size.x * 0.5f, hb.size.y * 0.5f});
                hpShadow.setPosition({centerPx.x + 2.0f, textY + 2.0f});
                window_.draw(hpShadow);
                // Main
                sf::Text hpMain(uiFont_, hpUtf8, 12);
                hpMain.setScale({2.0f, 2.0f});
                hpMain.setFillColor(sf::Color(180, 220, 255, static_cast<std::uint8_t>(textAlpha * 255.0f)));
                hpMain.setOrigin({hb.size.x * 0.5f, hb.size.y * 0.5f});
                hpMain.setPosition({centerPx.x, textY});
                window_.draw(hpMain);
                // Highlight
                sf::Text hpHi(uiFont_, hpUtf8, 12);
                hpHi.setScale({2.0f, 2.0f});
                hpHi.setFillColor(sf::Color(230, 245, 255, static_cast<std::uint8_t>(textAlpha * 130.0f)));
                hpHi.setOrigin({hb.size.x * 0.5f, hb.size.y * 0.5f});
                hpHi.setPosition({centerPx.x - 1.0f, textY - 1.0f});
                window_.draw(hpHi);
            }
        }

        // --- Crescent moon glyph at center (Phase 2) ---
        if (hpCrescentAlpha_ > 0.01f) {
            const auto ca = static_cast<std::uint8_t>(hpCrescentAlpha_ * 255.0f);
            const float moonR = 16.0f;
            // Full circle (moon body, silver-white)
            sf::CircleShape fullMoon(moonR, 24);
            fullMoon.setOrigin({moonR, moonR});
            fullMoon.setPosition(centerPx);
            fullMoon.setFillColor(sf::Color(220, 235, 255, ca));
            window_.draw(fullMoon);
            // Offset circle to "cut" the crescent shape (drawn subtractively via color blending)
            // We draw a second darker circle offset to create crescent illusion
            sf::CircleShape cutMoon(moonR * 0.85f, 24);
            cutMoon.setOrigin({moonR * 0.85f, moonR * 0.85f});
            cutMoon.setPosition({centerPx.x + moonR * 0.3f, centerPx.y - moonR * 0.1f});
            cutMoon.setFillColor(sf::Color(30, 40, 80, ca)); // dark overlay creates crescent
            window_.draw(cutMoon);
            // Crescent glow
            sf::CircleShape crescentGlow(moonR + 4.0f, 24);
            crescentGlow.setOrigin({moonR + 4.0f, moonR + 4.0f});
            crescentGlow.setPosition(centerPx);
            crescentGlow.setFillColor(sf::Color::Transparent);
            crescentGlow.setOutlineThickness(1.5f);
            crescentGlow.setOutlineColor(sf::Color(180, 210, 255, static_cast<std::uint8_t>(ca * 0.5f)));
            window_.draw(crescentGlow);
        }

        // --- Light motes ---
        if (!hpMotes_.empty() && hpMoteTex_.getSize().x > 0) {
            const auto msz = sf::Vector2f(hpMoteTex_.getSize());
            for (const auto& m : hpMotes_) {
                const float lifeRatio = 1.0f - m.life / m.maxLife;
                const auto alpha = static_cast<std::uint8_t>(lifeRatio * 200.0f);
                if (alpha == 0) continue;
                sf::Sprite mote(hpMoteTex_);
                mote.setOrigin({msz.x * 0.5f, msz.y * 0.5f});
                mote.setPosition(m.pos);
                mote.setScale({m.scale * lifeRatio, m.scale * lifeRatio});
                mote.setColor(sf::Color(255, 255, 255, alpha));
                window_.draw(mote);
            }
        }
    }

    // Sun card animation rendering
    if (sunAnimPending_) {
        const sf::Vector2f tengenPx = board_.cellToPixel(7, 7);
        const sf::Vector2f zoneTL = board_.cellToPixel(5, 5);
        const sf::Vector2f zoneBR = board_.cellToPixel(9, 9);

        // --- 5x5 zone amber glow + border ---
        {
            const float cellHalf = 18.0f;
            const float zLeft   = zoneTL.x - cellHalf;
            const float zTop    = zoneTL.y - cellHalf;
            const float zWidth  = zoneBR.x - zoneTL.x + cellHalf * 2.0f;
            const float zHeight = zoneBR.y - zoneTL.y + cellHalf * 2.0f;

            if (sunZoneGlowAlpha_ > 0.003f) {
                const auto za = static_cast<std::uint8_t>(sunZoneGlowAlpha_ * 255.0f);
                // Amber fill
                sf::RectangleShape zoneFill;
                zoneFill.setPosition({zLeft, zTop});
                zoneFill.setSize({zWidth, zHeight});
                zoneFill.setFillColor(sf::Color(240, 180, 50, za));
                window_.draw(zoneFill);

                // Bright inner fill
                sf::RectangleShape zoneInner;
                zoneInner.setPosition({zLeft + 10.0f, zTop + 10.0f});
                zoneInner.setSize({zWidth - 20.0f, zHeight - 20.0f});
                zoneInner.setFillColor(sf::Color(255, 210, 80, static_cast<std::uint8_t>(za * 0.45f)));
                window_.draw(zoneInner);

                // --- Zone border: outer glow + main line + inner line ---
                const float borderAlpha = sunZoneGlowAlpha_ + 0.15f; // border slightly brighter
                const auto ba = static_cast<std::uint8_t>(std::min(1.0f, borderAlpha) * 255.0f);

                // Outer glow border
                sf::RectangleShape borderGlow;
                borderGlow.setPosition({zLeft - 2.0f, zTop - 2.0f});
                borderGlow.setSize({zWidth + 4.0f, zHeight + 4.0f});
                borderGlow.setFillColor(sf::Color::Transparent);
                borderGlow.setOutlineThickness(3.5f);
                borderGlow.setOutlineColor(sf::Color(255, 185, 40, static_cast<std::uint8_t>(ba * 0.4f)));
                window_.draw(borderGlow);

                // Main border
                sf::RectangleShape borderMain;
                borderMain.setPosition({zLeft, zTop});
                borderMain.setSize({zWidth, zHeight});
                borderMain.setFillColor(sf::Color::Transparent);
                borderMain.setOutlineThickness(2.0f);
                borderMain.setOutlineColor(sf::Color(255, 210, 60, ba));
                window_.draw(borderMain);

                // Inner bright border
                sf::RectangleShape borderInner;
                borderInner.setPosition({zLeft + 3.0f, zTop + 3.0f});
                borderInner.setSize({zWidth - 6.0f, zHeight - 6.0f});
                borderInner.setFillColor(sf::Color::Transparent);
                borderInner.setOutlineThickness(1.0f);
                borderInner.setOutlineColor(sf::Color(255, 235, 140, static_cast<std::uint8_t>(ba * 0.5f)));
                window_.draw(borderInner);

                // Corner ornaments: 4 small glowing diamonds at zone corners
                const sf::Vector2f corners[] = {
                    {zLeft, zTop}, {zLeft + zWidth, zTop},
                    {zLeft, zTop + zHeight}, {zLeft + zWidth, zTop + zHeight}
                };
                for (const auto& c : corners) {
                    sf::RectangleShape diamond;
                    diamond.setPosition({c.x - 4.0f, c.y - 4.0f});
                    diamond.setSize({8.0f, 8.0f});
                    diamond.setRotation(sf::degrees(45.0f));
                    diamond.setFillColor(sf::Color(255, 230, 120, ba));
                    window_.draw(diamond);
                    // Tiny glow behind each corner
                    sf::CircleShape cornerGlow(6.0f, 8);
                    cornerGlow.setOrigin({6.0f, 6.0f});
                    cornerGlow.setPosition(c);
                    cornerGlow.setFillColor(sf::Color(255, 200, 50, static_cast<std::uint8_t>(ba * 0.35f)));
                    window_.draw(cornerGlow);
                }
            }
        }

        // --- Shockwave ring ---
        if (sunShockwaveAlpha_ > 0.01f && sunShockwaveRadius_ > 1.0f) {
            const auto swa = static_cast<std::uint8_t>(sunShockwaveAlpha_ * 255.0f);
            const float sr = sunShockwaveRadius_;
            // Outer glow
            sf::CircleShape swOuter(sr + 5.0f, 36);
            swOuter.setOrigin({sr + 5.0f, sr + 5.0f});
            swOuter.setPosition(tengenPx);
            swOuter.setFillColor(sf::Color::Transparent);
            swOuter.setOutlineThickness(5.0f);
            swOuter.setOutlineColor(sf::Color(255, 180, 30, static_cast<std::uint8_t>(swa * 0.3f)));
            window_.draw(swOuter);
            // Main ring
            sf::CircleShape swMain(sr, 36);
            swMain.setOrigin({sr, sr});
            swMain.setPosition(tengenPx);
            swMain.setFillColor(sf::Color::Transparent);
            swMain.setOutlineThickness(2.5f);
            swMain.setOutlineColor(sf::Color(255, 200, 40, swa));
            window_.draw(swMain);
            // Inner bright ring
            sf::CircleShape swInner(sr - 2.0f, 36);
            swInner.setOrigin({sr - 2.0f, sr - 2.0f});
            swInner.setPosition(tengenPx);
            swInner.setFillColor(sf::Color::Transparent);
            swInner.setOutlineThickness(1.0f);
            swInner.setOutlineColor(sf::Color(255, 230, 100, static_cast<std::uint8_t>(swa * 0.5f)));
            window_.draw(swInner);
        }

        // --- Sun rays (16 rays radiating from disc center) ---
        if (sunRayAlpha_ > 0.01f && sunRayTex_.getSize().x > 0) {
            const auto rsz = sf::Vector2f(sunRayTex_.getSize());
            const float discR = 32.0f * sunDiscScale_;
            const float rayY = tengenPx.y + sunDiscYOffset_;
            constexpr int kRays = 16;
            for (int i = 0; i < kRays; ++i) {
                const float angle = static_cast<float>(i) / static_cast<float>(kRays) * 2.0f * 3.14159265f;
                const float rayLen = 48.0f + sunDiscScale_ * 20.0f;
                sf::Sprite ray(sunRayTex_);
                ray.setOrigin({rsz.x * 0.5f, rsz.y * 0.1f}); // origin near base
                ray.setPosition({tengenPx.x + std::cos(angle) * discR * 0.75f,
                                 rayY + std::sin(angle) * discR * 0.75f});
                ray.setRotation(sf::degrees(angle * 180.0f / 3.14159265f + 90.0f));
                ray.setScale({0.7f + sunDiscScale_ * 0.3f, rayLen / rsz.y});
                ray.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(sunRayAlpha_ * 200.0f)));
                window_.draw(ray);
            }
        }

        // --- Sun disc ---
        if (sunDiscAlpha_ > 0.01f && sunDiscTex_.getSize().x > 0) {
            const auto dsz = sf::Vector2f(sunDiscTex_.getSize());
            const float discY = tengenPx.y + sunDiscYOffset_;
            // Outer glow
            sf::Sprite discGlow(sunDiscTex_);
            discGlow.setOrigin({dsz.x * 0.5f, dsz.y * 0.5f});
            discGlow.setPosition({tengenPx.x, discY});
            discGlow.setScale({sunDiscScale_ * 1.5f, sunDiscScale_ * 1.5f});
            discGlow.setRotation(sf::degrees(sunRotation_));
            discGlow.setColor(sf::Color(255, 200, 40, static_cast<std::uint8_t>(sunDiscAlpha_ * 80.0f)));
            window_.draw(discGlow);
            // Main disc
            sf::Sprite disc(sunDiscTex_);
            disc.setOrigin({dsz.x * 0.5f, dsz.y * 0.5f});
            disc.setPosition({tengenPx.x, discY});
            disc.setScale({sunDiscScale_, sunDiscScale_});
            disc.setRotation(sf::degrees(sunRotation_));
            disc.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(sunDiscAlpha_ * 255.0f)));
            window_.draw(disc);
        }

        // --- "光明普照" floating text ---
        if (sunDiscAlpha_ > 0.5f && sunDiscYOffset_ > -80.0f && fontLoaded_) {
            const float textAlpha = std::min(sunDiscAlpha_, sunRayAlpha_) * 0.72f;
            if (textAlpha > 0.03f) {
                const float textY = tengenPx.y + sunDiscYOffset_ - 50.0f;
                const std::string sunStr = "光明普照";
                const auto sunUtf8 = sf::String::fromUtf8(sunStr.begin(), sunStr.end());
                sf::Text sShadow(uiFont_, sunUtf8, 12);
                sShadow.setScale({2.0f, 2.0f});
                sShadow.setFillColor(sf::Color(60, 30, 5, static_cast<std::uint8_t>(textAlpha * 150.0f)));
                auto sb = sShadow.getLocalBounds();
                sShadow.setOrigin({sb.size.x * 0.5f, sb.size.y * 0.5f});
                sShadow.setPosition({tengenPx.x + 2.0f, textY + 2.0f});
                window_.draw(sShadow);
                sf::Text sMain(uiFont_, sunUtf8, 12);
                sMain.setScale({2.0f, 2.0f});
                sMain.setFillColor(sf::Color(255, 220, 80, static_cast<std::uint8_t>(textAlpha * 255.0f)));
                sMain.setOrigin({sb.size.x * 0.5f, sb.size.y * 0.5f});
                sMain.setPosition({tengenPx.x, textY});
                window_.draw(sMain);
                sf::Text sHi(uiFont_, sunUtf8, 12);
                sHi.setScale({2.0f, 2.0f});
                sHi.setFillColor(sf::Color(255, 250, 200, static_cast<std::uint8_t>(textAlpha * 140.0f)));
                sHi.setOrigin({sb.size.x * 0.5f, sb.size.y * 0.5f});
                sHi.setPosition({tengenPx.x - 1.0f, textY - 1.0f});
                window_.draw(sHi);
            }
        }

        // --- Sun motes ---
        if (!sunMotes_.empty() && sunMoteTex_.getSize().x > 0) {
            const auto msz = sf::Vector2f(sunMoteTex_.getSize());
            for (const auto& m : sunMotes_) {
                const float lifeRatio = 1.0f - m.life / m.maxLife;
                const auto alpha = static_cast<std::uint8_t>(lifeRatio * 200.0f);
                if (alpha == 0) continue;
                sf::Sprite mote(sunMoteTex_);
                mote.setOrigin({msz.x * 0.5f, msz.y * 0.5f});
                mote.setPosition(m.pos);
                mote.setScale({m.scale * lifeRatio, m.scale * lifeRatio});
                mote.setColor(sf::Color(255, 255, 255, alpha));
                window_.draw(mote);
            }
        }
    }

    // Pixel sparks from stone placement
    for (const auto& s : sparks_) {
        const float lifeRatio = s.life / s.maxLife;
        const std::uint8_t alpha = static_cast<std::uint8_t>(lifeRatio * 255.0f);
        sf::RectangleShape r;
        r.setPosition({s.pos.x - s.size * 0.5f, s.pos.y - s.size * 0.5f});
        r.setSize({s.size, s.size});
        r.setFillColor(sf::Color(s.color.r, s.color.g, s.color.b, alpha));
        window_.draw(r);
    }

    sf::RectangleShape statusCard;
    statusCard.setPosition({790.0f, 285.0f});
    statusCard.setSize({412.0f, 220.0f});
    statusCard.setFillColor(sf::Color(242, 236, 222));
    statusCard.setOutlineThickness(1.0f);
    statusCard.setOutlineColor(sf::Color(196, 176, 140));
    window_.draw(statusCard);

    drawText("对局状态", {816.0f, 309.0f}, 24, sf::Color(58, 46, 32), sf::Text::Bold);
    drawText(statusLine(), {816.0f, 355.0f}, 21, sf::Color(83, 63, 24));

    // Active card effect indicators
    {
        std::string fx;
        if (foolActive_) fx += " [愚者护盾]";
        if (hierophantRemaining_ > 0) fx += " [规则束缚 x" + std::to_string(hierophantRemaining_) + "]";
        if (temperanceRemaining_ > 0) fx += " [调和之道 x" + std::to_string(temperanceRemaining_) + "]";
        if (strengthProtectionRemaining_ > 0 && (!networkMode_ || iAmPicker_)) fx += " [坚不可摧 x" + std::to_string(strengthProtectionRemaining_) + "]";
        if (moonActive_) fx += " [月之迷雾]";
        if (!fx.empty()) {
            drawText(fx, {816.0f, 382.0f}, 15, sf::Color(160, 110, 40));
        }
    }

    // Opponent character in status card (AI mode only)
    if (aiEnabled_ && opponentTex_.getSize().x > 0) {
        const sf::Texture* tex = &opponentTex_;
        switch (opponentAnim_) {
        case OpponentAnim::Think: if (opponentThinkTex_.getSize().x > 0) tex = &opponentThinkTex_; break;
        case OpponentAnim::Win:   if (opponentWinTex_.getSize().x > 0)   tex = &opponentWinTex_;   break;
        case OpponentAnim::Lose:  if (opponentLoseTex_.getSize().x > 0)  tex = &opponentLoseTex_;  break;
        default: break;
        }
        sf::Sprite charSpr(*tex);
        charSpr.setTextureRect(sf::IntRect(
            {opponentCurrentFrame_ * opponentFrameWidth_, 0},
            {opponentFrameWidth_, opponentFrameHeight_}));
        const float sprW = static_cast<float>(opponentFrameWidth_) * opponentScale_;
        const float sprH = static_cast<float>(opponentFrameHeight_) * opponentScale_;
        constexpr float cardRight = 1202.0f;
        constexpr float cardTop = 285.0f;
        constexpr float cardH = 220.0f;
        constexpr float cardBottom = cardTop + cardH;  // 505
        constexpr float bottomPad = 10.0f;
        const float sprX = cardRight - sprW + opponentOffsetX_;
        const float sprY = cardBottom - bottomPad - sprH;
        charSpr.setPosition({sprX, sprY});
        charSpr.setScale({opponentScale_, opponentScale_});
        window_.draw(charSpr);

        // Speech bubble above character — frosted glass style
        if (!speechText_.empty() && fontLoaded_) {
            const float sprCenterX = sprX + sprW * 0.5f;

            const auto utf8 = sf::String::fromUtf8(speechText_.begin(), speechText_.end());
            sf::Text bubbleText(uiFont_, utf8, 17);
            bubbleText.setStyle(sf::Text::Bold);

            const float padX = 16.0f;
            const float padY = 8.0f;
            const sf::FloatRect textBounds = bubbleText.getLocalBounds();
            const float bw = textBounds.size.x + padX * 2.0f;
            const float bh = textBounds.size.y + padY * 2.0f;
            const float gap = 5.0f;
            const float bx = std::max(790.0f, sprCenterX - bw * 0.5f);
            const float by = sprY - bh - gap;
            const float bevel = 2.0f;
            const float triH = 6.0f;
            const float triW = 10.0f;

            // Drop shadow
            sf::RectangleShape bShadow;
            bShadow.setPosition({bx + 3.0f, by + 3.0f});
            bShadow.setSize({bw, bh});
            bShadow.setFillColor(sf::Color(10, 8, 20, 70));
            window_.draw(bShadow);

            // Glass body
            sf::RectangleShape body;
            body.setPosition({bx, by});
            body.setSize({bw, bh});
            body.setFillColor(sf::Color(22, 18, 32, 160));
            window_.draw(body);

            // Noise
            if (noiseTex_.getSize().x > 0) {
                sf::Sprite ns(noiseTex_);
                ns.setPosition({bx, by});
                ns.setTextureRect(sf::IntRect({0, 0}, {static_cast<int>(bw), static_cast<int>(bh)}));
                ns.setColor(sf::Color(255, 255, 255, 10));
                window_.draw(ns);
            }

            // Top bevel
            { sf::RectangleShape t; t.setPosition({bx, by}); t.setSize({bw, bevel});
              t.setFillColor(sf::Color(255, 255, 255, 50)); window_.draw(t); }
            // Left bevel
            { sf::RectangleShape l; l.setPosition({bx, by}); l.setSize({bevel, bh});
              l.setFillColor(sf::Color(255, 255, 255, 35)); window_.draw(l); }
            // Bottom bevel
            { sf::RectangleShape b; b.setPosition({bx, by + bh - bevel}); b.setSize({bw, bevel});
              b.setFillColor(sf::Color(0, 0, 0, 40)); window_.draw(b); }
            // Right bevel
            { sf::RectangleShape r; r.setPosition({bx + bw - bevel, by}); r.setSize({bevel, bh});
              r.setFillColor(sf::Color(0, 0, 0, 25)); window_.draw(r); }

            // Triangle pointer (frosted)
            sf::ConvexShape tri;
            tri.setPointCount(3);
            tri.setPoint(0, {sprCenterX, by + bh + triH});
            tri.setPoint(1, {sprCenterX - triW * 0.5f, by + bh});
            tri.setPoint(2, {sprCenterX + triW * 0.5f, by + bh});
            tri.setFillColor(sf::Color(22, 18, 32, 160));
            window_.draw(tri);

            // Text
            bubbleText.setPosition({bx + padX, by + padY - textBounds.position.y});
            bubbleText.setFillColor(sf::Color(238, 228, 210));
            window_.draw(bubbleText);
        }
    }

    // Countdown timer ring — only shown for current player in network mode
    if ((!networkMode_ || isMyTurn()) && !gameOver_ && !gameReady_ && cardEventState_ == CardEventState::Idle) {
        constexpr float tCx = 996.0f;
        constexpr float tCy = 545.0f;
        constexpr float tR = 26.0f;
        constexpr int segs = 16;
        const float effectiveLimit = static_cast<float>(hermitRemaining_ > 0 ? turnTimeLimit_ * 2 : turnTimeLimit_);
        const float elapsed = turnTimer_.getElapsedTime().asSeconds();
        const float remaining = std::max(0.0f, effectiveLimit - elapsed);
        const float progress = remaining / effectiveLimit;

        sf::Color segColor;
        if (remaining > 5.0f)      segColor = {90, 185, 90};
        else if (remaining > 3.0f) segColor = {225, 175, 40};
        else {
            const float flicker = std::sin(atmTime * 8.0f) * 0.3f + 0.7f;
            segColor = sf::Color(220, 55, 45,
                static_cast<std::uint8_t>(255.0f * flicker));
        }
        const int filled = static_cast<int>(progress * static_cast<float>(segs));

        for (int i = 0; i < segs; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(segs) * 6.28318f - 1.5708f;
            const float bx = tCx + std::cos(a) * tR;
            const float by = tCy + std::sin(a) * tR;
            sf::RectangleShape block;
            block.setPosition({bx - 3.0f, by - 3.0f});
            block.setSize({6.0f, 6.0f});
            block.setFillColor(i < filled ? segColor : sf::Color(50, 44, 40));
            window_.draw(block);
        }

        // Center seconds number
        const auto secStr = std::to_string(static_cast<int>(std::ceil(remaining)));
        drawCenteredText(secStr, sf::FloatRect({tCx - 24.0f, tCy - 16.0f}, {48.0f, 32.0f}),
                         20, segColor, sf::Text::Bold);
    }

    const sf::Vector2f mousePosition = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));

    // Helper: draw a frosted glass button
    auto drawGlassBtn = [&](sf::Vector2f pos, sf::Vector2f size, sf::Color tint, std::string_view label,
                            bool hovered, bool pressed) {
        constexpr float gBevel = 2.5f;
        constexpr float gShadow = 3.0f;
        const float gScale = pressed ? 0.92f : 1.0f;

        const float dW = size.x * gScale;
        const float dH = size.y * gScale;
        const float dX = pos.x + (size.x - dW) * 0.5f;
        const float dY = pos.y + (size.y - dH) * 0.5f;
        const sf::Vector2f dPos = {dX, dY};
        const sf::Vector2f dSize = {dW, dH};

        // Drop shadow
        {
            sf::RectangleShape sh;
            sh.setPosition({dX + gShadow, dY + gShadow});
            sh.setSize(dSize);
            sh.setFillColor(sf::Color(20, 16, 28, 80));
            window_.draw(sh);
        }
        // Body
        {
            sf::RectangleShape body;
            body.setPosition(dPos);
            body.setSize(dSize);
            body.setFillColor(sf::Color(tint.r, tint.g, tint.b,
                                        hovered ? static_cast<std::uint8_t>(std::min(255, tint.a + 30)) : tint.a));
            window_.draw(body);
        }
        // Noise
        if (noiseTex_.getSize().x > 0) {
            sf::Sprite ns(noiseTex_);
            ns.setPosition(dPos);
            ns.setTextureRect(sf::IntRect({0, 0}, {static_cast<int>(dW), static_cast<int>(dH)}));
            ns.setColor(sf::Color(255, 255, 255, 10));
            window_.draw(ns);
        }
        // Top bevel
        { sf::RectangleShape t; t.setPosition(dPos); t.setSize({dW, gBevel});
          t.setFillColor(sf::Color(255, 255, 255, 80)); window_.draw(t); }
        // Left bevel
        { sf::RectangleShape l; l.setPosition(dPos); l.setSize({gBevel, dH});
          l.setFillColor(sf::Color(255, 255, 255, 60)); window_.draw(l); }
        // Bottom bevel
        { sf::RectangleShape b; b.setPosition({dX, dY + dH - gBevel}); b.setSize({dW, gBevel});
          b.setFillColor(sf::Color(0, 0, 0, 60)); window_.draw(b); }
        // Right bevel
        { sf::RectangleShape r; r.setPosition({dX + dW - gBevel, dY}); r.setSize({gBevel, dH});
          r.setFillColor(sf::Color(0, 0, 0, 40)); window_.draw(r); }
        // Label
        drawCenteredText(label, sf::FloatRect(dPos, dSize), static_cast<unsigned>(20.0f * gScale),
                         hovered ? sf::Color(255, 252, 240) : sf::Color(240, 232, 215), sf::Text::Bold);
        // Hover glow
        if (hovered && !pressed) {
            sf::RectangleShape glow;
            glow.setPosition(dPos);
            glow.setSize(dSize);
            glow.setFillColor(sf::Color(255, 255, 240, 12));
            window_.draw(glow);
        }
    };

    if (networkMode_) {
        auto undoLabel = std::string("悔棋 (") + std::to_string(remainingUndos_) + ")";
        const bool undoAvailable = remainingUndos_ > 0 && !gameOver_ && !isMyTurn();

        drawGlassBtn({790.0f, 600.0f}, {190.0f, 62.0f},
                     sf::Color(160, 70, 60, 130), "断开连接", isMouseOver(UiButton{sf::FloatRect({790.0f, 600.0f}, {190.0f, 62.0f}), "", {}, {}}, mousePosition), buttonAnimIndex_ == 310);
        drawGlassBtn({1002.0f, 600.0f}, {190.0f, 62.0f},
                     sf::Color(130, 95, 55, 130), "重新开始", isMouseOver(UiButton{sf::FloatRect({1002.0f, 600.0f}, {190.0f, 62.0f}), "", {}, {}}, mousePosition), buttonAnimIndex_ == 311);
        drawGlassBtn({790.0f, 674.0f}, {196.0f, 62.0f},
                     undoAvailable ? sf::Color(80, 120, 90, 130) : sf::Color(60, 60, 60, 100),
                     undoLabel, isMouseOver(UiButton{sf::FloatRect({790.0f, 674.0f}, {196.0f, 62.0f}), "", {}, {}}, mousePosition), buttonAnimIndex_ == 312);
        drawGlassBtn({1006.0f, 674.0f}, {196.0f, 62.0f},
                     sf::Color(150, 65, 55, 130), "投降", isMouseOver(UiButton{sf::FloatRect({1006.0f, 674.0f}, {196.0f, 62.0f}), "", {}, {}}, mousePosition), buttonAnimIndex_ == 313);
    } else {
        auto undoLabel = std::string("悔棋");
        if (aiEnabled_ && remainingUndos_ >= 0) {
            undoLabel += " (" + std::to_string(remainingUndos_) + ")";
        }
        const bool undoAvailable = (!aiEnabled_ || remainingUndos_ != 0) && !gameOver_;

        drawGlassBtn({790.0f, 600.0f}, {190.0f, 62.0f},
                     sf::Color(65, 80, 115, 130), "返回主菜单", isMouseOver(UiButton{sf::FloatRect({790.0f, 600.0f}, {190.0f, 62.0f}), "", {}, {}}, mousePosition), buttonAnimIndex_ == 302);
        drawGlassBtn({1002.0f, 600.0f}, {190.0f, 62.0f},
                     sf::Color(130, 95, 55, 130), "重新开始", isMouseOver(UiButton{sf::FloatRect({1002.0f, 600.0f}, {190.0f, 62.0f}), "", {}, {}}, mousePosition), buttonAnimIndex_ == 301);
        drawGlassBtn({790.0f, 674.0f}, {402.0f, 62.0f},
                     undoAvailable ? sf::Color(80, 120, 90, 130) : sf::Color(60, 60, 60, 100),
                     undoLabel, isMouseOver(UiButton{sf::FloatRect({790.0f, 674.0f}, {402.0f, 62.0f}), "", {}, {}}, mousePosition), buttonAnimIndex_ == 300);
    }

    // Intro fade-in veil: re-draw background over UI, fading out to reveal elements
    if (gameIntroAlpha_ < 1.0f && gameBgTex_.getSize().x > 0) {
        sf::Sprite veil(gameBgTex_);
        const auto texSize = gameBgTex_.getSize();
        veil.setScale({1280.0f / static_cast<float>(texSize.x),
                       820.0f / static_cast<float>(texSize.y)});
        veil.setColor(sf::Color(255, 255, 255,
            static_cast<std::uint8_t>((1.0f - gameIntroAlpha_) * 255.0f)));
        window_.draw(veil);
    }

    // Ready prompt — click anywhere to begin
    if (gameReady_ && fontLoaded_) {
        const float pulse = std::sin(atmosphereClock_.getElapsedTime().asSeconds() * 2.5f) * 0.3f + 0.7f;
        const auto promptColor = sf::Color(255, 252, 235,
            static_cast<std::uint8_t>(180.0f * pulse));
        // Drop shadow
        drawCenteredText("点击屏幕任意位置开始游戏",
            sf::FloatRect({5.0f, 355.0f}, {1280.0f, 60.0f}), 28,
            sf::Color(20, 16, 30, 140), sf::Text::Bold);
        drawCenteredText("点击屏幕任意位置开始游戏",
            sf::FloatRect({0.0f, 350.0f}, {1280.0f, 60.0f}), 28,
            promptColor, sf::Text::Bold);
    }

    // Card event overlay (all phases)
    if (cardEventState_ != CardEventState::Idle && fontLoaded_) {
        // Ensure card textures are loaded (network join may bypass startMatch)
        if (cardTextures_[0].getSize().x == 0) {
            loadCardTextures();
        }

        constexpr float cardScale = 2.8f;
        constexpr float cardW = 73.0f * cardScale;
        constexpr float cardH = 113.0f * cardScale;
        constexpr float spacing = 100.0f;
        constexpr float totalW = cardW * 3.0f + spacing * 2.0f;
        constexpr float startX = (1280.0f - totalW) * 0.5f;
        constexpr float startY = 320.0f;
        constexpr float boxH = 98.0f;
        constexpr float boxGap = 10.0f;
        const auto mousePos = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));

        // --- Dark overlay ---
        {
            const auto da = static_cast<std::uint8_t>(darkenAlpha_ * 255.0f);
            if (da > 0) {
                sf::RectangleShape overlay;
                overlay.setPosition({0.0f, 0.0f});
                overlay.setSize({1280.0f, 820.0f});
                overlay.setFillColor(sf::Color(6, 4, 14, da));
                window_.draw(overlay);
            }
        }

        // --- Card name/desc lambdas ---
        auto cardNameLine = [](CardType c) -> std::string {
            switch (c) {
            case CardType::Fool:           return "愚者 · 未知之旅";
            case CardType::Magician:       return "魔术师 · 妙手生花";
            case CardType::HighPriestess:  return "女祭司 · 圣域结界";
            case CardType::Empress:        return "皇后 · 生机盎然";
            case CardType::Emperor:        return "皇帝 · 帝王号令";
            case CardType::Hierophant:     return "教皇 · 规则束缚";
            case CardType::Lovers:         return "恋人 · 命运交织";
            case CardType::Chariot:        return "战车 · 冲锋陷阵";
            case CardType::Strength:       return "力量 · 坚不可摧";
            case CardType::Hermit:         return "隐者 · 明镜止水";
            case CardType::WheelOfFortune: return "命运之轮 · 时来运转";
            case CardType::Justice:        return "正义 · 天道轮回";
            case CardType::HangedMan:      return "倒吊人 · 自我牺牲";
            case CardType::Death:          return "死神 · 死神降临";
            case CardType::Temperance:     return "节制 · 调和之道";
            case CardType::Devil:          return "恶魔 · 恶魔契约";
            case CardType::Tower:          return "塔 · 天崩地裂";
            case CardType::Star:           return "星星 · 希望之星";
            case CardType::Moon:           return "月亮 · 月之迷雾";
            case CardType::Sun:            return "太阳 · 光明普照";
            case CardType::Judgement:      return "审判 · 最终审判";
            case CardType::World:          return "世界 · 周而复始";
            default: return "";
            }
        };

        auto cardDesc = [](CardType c) -> std::string {
            switch (c) {
            case CardType::Fool:           return "跳过下一次\n障碍物刷新事件";
            case CardType::Magician:       return "在最后落子的\n对称位置免费放一颗棋";
            case CardType::HighPriestess:  return "清除最后落子所在\n整行整列的棋子与障碍";
            case CardType::Empress:        return "在你棋子相邻空位\n免费放置一颗棋子";
            case CardType::Emperor:        return "移除棋盘上\n任意2个障碍物";
            case CardType::Hierophant:     return "接下来2回合\n双方限中心9x9区域";
            case CardType::Lovers:         return "随机交换棋盘上\n1颗黑棋和1颗白棋";
            case CardType::Chariot:        return "下一颗棋子可将\n对手棋子推离1格";
            case CardType::Strength:       return "下一颗棋子3回合内\n计算连子时视为2颗";
            case CardType::Hermit:         return "接下来2回合\n双方思考时间翻倍";
            case CardType::WheelOfFortune: return "所有障碍物\n随机重新分布";
            case CardType::Justice:        return "双方各移除\n1颗最早的棋子";
            case CardType::HangedMan:      return "牺牲自己1颗棋子\n移除对手2颗棋子";
            case CardType::Death:          return "清除最后落子周围\n3x3区域的所有棋子";
            case CardType::Temperance:     return "接下来2回合\n障碍物不阻断你的五连";
            case CardType::Devil:          return "移除对手\n最长连子中的1颗棋子";
            case CardType::Tower:          return "清除全部障碍物\n并重新随机生成";
            case CardType::Star:           return "高亮显示AI推荐的\n最佳落子位置";
            case CardType::Moon:           return "下回合对方的落子\n被随机偏移1格";
            case CardType::Sun:            return "清除天元周围\n5x5区域的所有障碍物";
            case CardType::Judgement:      return "棋子总数达20时\n清除所有障碍物";
            case CardType::World:          return "丧失全部悔棋机会\n本局再也无法悔棋";
            default: return "";
            }
        };

        // --- Messenger sprite (15-frame spritesheet, 224px per frame) ---
        const bool showMessenger = (cardEventState_ >= CardEventState::MessengerBig &&
                                     cardEventState_ != CardEventState::Applied);
        if (showMessenger && messengerTex_.getSize().x > 0) {
            const auto msz = messengerTex_.getSize();
            constexpr int kMessengerFrames = 15;
            const int frameW = static_cast<int>(msz.x) / kMessengerFrames;
            const int frameH = static_cast<int>(msz.y);

            // Messenger stays at large scale throughout the event (fades out in Reveal)
            const float mScale = 580.0f / static_cast<float>(frameH);
            const float mW = static_cast<float>(frameW) * mScale;
            const float mH = static_cast<float>(frameH) * mScale;
            const float floatY = std::sin(messengerFloatClock_.getElapsedTime().asSeconds() * 1.5f) * 6.0f;

            // Cycle frames at ~8fps
            const float mElapsed = messengerFloatClock_.getElapsedTime().asSeconds();
            const int curFrame = static_cast<int>(mElapsed * 8.0f) % kMessengerFrames;

            float mAlpha = messengerAlpha_;

            // Ghostly presence behind cards; dissipate fade during Reveal
            if (cardEventState_ == CardEventState::DealCards) {
                mAlpha *= 1.0f - cardDealProgress_ * 0.65f;
            } else if (cardEventState_ >= CardEventState::FlipCards && cardEventState_ <= CardEventState::Choosing) {
                mAlpha *= 0.35f;
            } else if (cardEventState_ == CardEventState::Reveal) {
                const float rElapsed = cardEventClock_.getElapsedTime().asSeconds();
                if (rElapsed < 1.45f) {
                    mAlpha *= 0.35f;  // ghost behind cards during Confirm→Bloom
                } else {
                    const float dissipateProg = std::min(1.0f, (rElapsed - 1.45f) / 0.3f);
                    mAlpha *= 0.35f * (1.0f - dissipateProg);
                }
            }

            sf::Sprite mSpr(messengerTex_);
            mSpr.setTextureRect(sf::IntRect({curFrame * frameW, 0}, {frameW, frameH}));
            mSpr.setPosition({messengerPos_.x - mW * 0.5f, messengerPos_.y - mH + floatY});
            mSpr.setScale({mScale, mScale});
            mSpr.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(mAlpha * 255.0f)));
            window_.draw(mSpr);

            // Messenger speech bubble to the right of the sprite
            if (!cardEventSpeech_.empty() && mAlpha > 0.1f) {
                // Fade out bubble after 2.5s in MessengerWait, or always show during MessengerBig
                float speechAlpha = 1.0f;
                if (cardEventState_ == CardEventState::MessengerWait) {
                    const float waitTime = cardEventClock_.getElapsedTime().asSeconds();
                    if (waitTime > 2.5f) {
                        speechAlpha = 1.0f - std::min(1.0f, (waitTime - 2.5f) / 0.5f);
                    }
                }
                if (speechAlpha > 0.0f) {
                const auto utf8 = sf::String::fromUtf8(cardEventSpeech_.begin(), cardEventSpeech_.end());
                sf::Text bubbleText(uiFont_, utf8, 17);
                bubbleText.setStyle(sf::Text::Bold);
                const float padX = 16.0f;
                const float padY = 9.0f;
                const auto tb = bubbleText.getLocalBounds();
                const float bw = tb.size.x + padX * 2.0f;
                const float bh = tb.size.y + padY * 2.0f;
                const float gap = 18.0f;
                // Position bubble to the right of the messenger, vertically centered on the upper half
                const float bubbleX = messengerPos_.x + mW * 0.5f + gap;
                const float bubbleY = messengerPos_.y - mH * 0.6f + floatY;
                const float bx = std::min(bubbleX, 1280.0f - bw - 10.0f); // clamp to screen
                const float by = bubbleY;

                const auto sa = static_cast<std::uint8_t>(speechAlpha * mAlpha * 255.0f);

                // Drop shadow
                {
                    sf::RectangleShape sh;
                    sh.setPosition({bx + 3.0f, by + 3.0f});
                    sh.setSize({bw, bh});
                    sh.setFillColor(sf::Color(8, 4, 16, sa / 2));
                    window_.draw(sh);
                }

                // Bubble body — brighter, higher contrast against dark overlay
                {
                    sf::RectangleShape body;
                    body.setPosition({bx, by});
                    body.setSize({bw, bh});
                    body.setFillColor(sf::Color(55, 40, 75, static_cast<std::uint8_t>(speechAlpha * mAlpha * 210.0f)));
                    window_.draw(body);
                }

                // Noise overlay
                if (noiseTex_.getSize().x > 0) {
                    sf::Sprite ns(noiseTex_);
                    ns.setPosition({bx, by});
                    ns.setTextureRect(sf::IntRect({0, 0}, {static_cast<int>(bw), static_cast<int>(bh)}));
                    ns.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(speechAlpha * mAlpha * 14.0f)));
                    window_.draw(ns);
                }

                // Top/left bright bevel
                {
                    const float bevel = 2.0f;
                    sf::RectangleShape topB;
                    topB.setPosition({bx, by});
                    topB.setSize({bw, bevel});
                    topB.setFillColor(sf::Color(160, 140, 200, static_cast<std::uint8_t>(speechAlpha * mAlpha * 80.0f)));
                    window_.draw(topB);
                    sf::RectangleShape leftB;
                    leftB.setPosition({bx, by});
                    leftB.setSize({bevel, bh});
                    leftB.setFillColor(sf::Color(140, 120, 180, static_cast<std::uint8_t>(speechAlpha * mAlpha * 60.0f)));
                    window_.draw(leftB);
                }

                // Bottom/right dark bevel
                {
                    const float bevel = 2.0f;
                    sf::RectangleShape botB;
                    botB.setPosition({bx, by + bh - bevel});
                    botB.setSize({bw, bevel});
                    botB.setFillColor(sf::Color(8, 4, 16, static_cast<std::uint8_t>(speechAlpha * mAlpha * 80.0f)));
                    window_.draw(botB);
                    sf::RectangleShape rightB;
                    rightB.setPosition({bx + bw - bevel, by});
                    rightB.setSize({bevel, bh});
                    rightB.setFillColor(sf::Color(8, 4, 16, static_cast<std::uint8_t>(speechAlpha * mAlpha * 60.0f)));
                    window_.draw(rightB);
                }

                // Border outline
                {
                    sf::RectangleShape rim;
                    rim.setPosition({bx, by});
                    rim.setSize({bw, bh});
                    rim.setFillColor(sf::Color::Transparent);
                    rim.setOutlineThickness(2.0f);
                    rim.setOutlineColor(sf::Color(100, 85, 135, static_cast<std::uint8_t>(speechAlpha * mAlpha * 180.0f)));
                    window_.draw(rim);
                }

                // Triangle pointing left toward messenger (pointing at right edge of messenger)
                {
                    const float triW = 10.0f;
                    const float triMidY = by + bh * 0.5f;
                    sf::ConvexShape tri;
                    tri.setPointCount(3);
                    tri.setPoint(0, {bx, triMidY});
                    tri.setPoint(1, {bx + triW, triMidY - 7.0f});
                    tri.setPoint(2, {bx + triW, triMidY + 7.0f});
                    tri.setFillColor(sf::Color(55, 40, 75, static_cast<std::uint8_t>(speechAlpha * mAlpha * 210.0f)));
                    window_.draw(tri);
                }

                // Text
                bubbleText.setPosition({bx + padX, by + padY - tb.position.y});
                bubbleText.setFillColor(sf::Color(250, 242, 220, sa));
                window_.draw(bubbleText);
                }  // speechAlpha > 0
            }
        }

        // --- Helper: draw rounded fill ---
        auto drawRoundedFill = [&](float rx, float ry, float rw, float rh, float rad, sf::Color c) {
            sf::RectangleShape body;
            body.setPosition({rx + rad, ry});
            body.setSize({rw - rad * 2.0f, rh});
            body.setFillColor(c);
            window_.draw(body);
            sf::RectangleShape strip;
            strip.setPosition({rx, ry + rad});
            strip.setSize({rw, rh - rad * 2.0f});
            strip.setFillColor(c);
            window_.draw(strip);
            const float d = rad * 2.0f;
            sf::CircleShape corner(rad);
            corner.setFillColor(c);
            corner.setPosition({rx, ry}); window_.draw(corner);
            corner.setPosition({rx + rw - d, ry}); window_.draw(corner);
            corner.setPosition({rx, ry + rh - d}); window_.draw(corner);
            corner.setPosition({rx + rw - d, ry + rh - d}); window_.draw(corner);
        };

        // --- Helper: draw info box ---
        auto drawInfoBox = [&](float bx, float by, float bw, float bh, int cardIdx, bool hovered, float boxAlpha) {
            constexpr float cr = 8.0f;
            const auto a = static_cast<std::uint8_t>(boxAlpha * 255.0f);
            if (a == 0) return;

            drawRoundedFill(bx + 3.0f, by + 5.0f, bw, bh, cr,
                sf::Color(4, 2, 12, hovered ? static_cast<std::uint8_t>(140.0f * boxAlpha) : static_cast<std::uint8_t>(100.0f * boxAlpha)));
            const auto glassFill = hovered
                ? sf::Color(36, 26, 62, static_cast<std::uint8_t>(215.0f * boxAlpha))
                : sf::Color(26, 18, 48, static_cast<std::uint8_t>(185.0f * boxAlpha));
            drawRoundedFill(bx, by, bw, bh, cr, glassFill);

            if (noiseTex_.getSize().x > 0) {
                sf::Sprite ns(noiseTex_);
                ns.setPosition({bx, by});
                const auto nsz = noiseTex_.getSize();
                ns.setScale({bw / static_cast<float>(nsz.x), bh / static_cast<float>(nsz.y)});
                ns.setColor(sf::Color(255, 255, 255, hovered ? static_cast<std::uint8_t>(18.0f * boxAlpha) : static_cast<std::uint8_t>(12.0f * boxAlpha)));
                window_.draw(ns);
            }
            {
                sf::RectangleShape rim;
                rim.setPosition({bx, by});
                rim.setSize({bw, bh});
                rim.setFillColor(sf::Color::Transparent);
                rim.setOutlineThickness(2.5f);
                rim.setOutlineColor(sf::Color(8, 4, 20, hovered ? static_cast<std::uint8_t>(160.0f * boxAlpha) : static_cast<std::uint8_t>(110.0f * boxAlpha)));
                window_.draw(rim);
            }
            {
                const float blw = 2.0f;
                sf::RectangleShape topB;
                topB.setPosition({bx + cr, by + 1.0f});
                topB.setSize({bw - cr * 2.0f, blw});
                topB.setFillColor(hovered ? sf::Color(180, 155, 225, static_cast<std::uint8_t>(100.0f * boxAlpha))
                                          : sf::Color(140, 115, 185, static_cast<std::uint8_t>(60.0f * boxAlpha)));
                window_.draw(topB);
                sf::RectangleShape leftB;
                leftB.setPosition({bx + 1.0f, by + cr});
                leftB.setSize({blw, bh - cr * 2.0f});
                leftB.setFillColor(hovered ? sf::Color(180, 155, 225, static_cast<std::uint8_t>(90.0f * boxAlpha))
                                           : sf::Color(140, 115, 185, static_cast<std::uint8_t>(50.0f * boxAlpha)));
                window_.draw(leftB);
            }
            {
                const float blw = 2.0f;
                sf::RectangleShape botB;
                botB.setPosition({bx + cr, by + bh - blw});
                botB.setSize({bw - cr * 2.0f, blw});
                botB.setFillColor(sf::Color(4, 2, 12, hovered ? static_cast<std::uint8_t>(100.0f * boxAlpha) : static_cast<std::uint8_t>(65.0f * boxAlpha)));
                window_.draw(botB);
                sf::RectangleShape rightB;
                rightB.setPosition({bx + bw - blw, by + cr});
                rightB.setSize({blw, bh - cr * 2.0f});
                rightB.setFillColor(sf::Color(4, 2, 12, hovered ? static_cast<std::uint8_t>(90.0f * boxAlpha) : static_cast<std::uint8_t>(55.0f * boxAlpha)));
                window_.draw(rightB);
            }
            drawCenteredText(cardNameLine(drawnCards_[cardIdx]),
                sf::FloatRect({bx + 4.0f, by + 8.0f}, {bw - 8.0f, 22.0f}), 15,
                sf::Color(250, 225, 160, a), sf::Text::Bold);
            drawCenteredText(cardDesc(drawnCards_[cardIdx]),
                sf::FloatRect({bx + 4.0f, by + 30.0f}, {bw - 8.0f, bh - 36.0f}), 14,
                hovered ? sf::Color(245, 235, 215, a) : sf::Color(210, 195, 175, a));
        };

        // --- Cards drawing ---
        const bool showCards = cardEventState_ >= CardEventState::DealCards && cardEventState_ != CardEventState::Applied;
        if (showCards) {
            for (int i = 0; i < kCardsPerEvent; ++i) {
                const float cx = startX + static_cast<float>(i) * (cardW + spacing);
                const float cy = startY;
                const float boxY = cy + cardH + boxGap;

                // Plan A Reveal: Confirm → Eliminate → Ascend → Bloom → Dissipate
                float revealScale = 1.0f;
                float revealX = cx;
                float revealY = cy;
                float cardAlpha = 1.0f;
                float boxAlpha = 1.0f;
                bool isChosen = (i == chosenCardIdx_);
                bool golden = false;

                if (cardEventState_ == CardEventState::Reveal) {
                    const float rElapsed = cardEventClock_.getElapsedTime().asSeconds();

                    if (isChosen) {
                        const float homeCx = cx + cardW * 0.5f;
                        const float homeCy = cy + cardH * 0.5f;

                        if (rElapsed < 0.55f) {
                            // Confirm (0-0.2): golden glow. Eliminate (0.2-0.55): box fades
                            if (rElapsed < 0.2f) golden = true;
                            if (rElapsed > 0.2f) {
                                const float elimProg = (rElapsed - 0.2f) / 0.35f;
                                boxAlpha = 1.0f - elimProg;
                            }
                        } else if (rElapsed < 1.05f) {
                            // Ascend: ease-in-out to screen center, no scale change
                            boxAlpha = 0.0f;
                            const float ascendProg = (rElapsed - 0.55f) / 0.5f;
                            float t;
                            if (ascendProg < 0.5f)
                                t = 2.0f * ascendProg * ascendProg;
                            else
                                t = 1.0f - std::pow(-2.0f * ascendProg + 2.0f, 2.0f) / 2.0f;
                            const float curCx = homeCx + (640.0f - homeCx) * t;
                            const float curCy = homeCy + (410.0f - homeCy) * t;
                            revealX = curCx - cardW * 0.5f;
                            revealY = curCy - cardH * 0.5f;
                        } else if (rElapsed < 1.45f) {
                            // Bloom: overshoot scale at center 1→1.25→1.1
                            boxAlpha = 0.0f;
                            const float bloomProg = (rElapsed - 1.05f) / 0.4f;
                            if (bloomProg < 0.65f) {
                                const float bt = bloomProg / 0.65f;
                                revealScale = 1.0f + 0.25f * (1.0f - (1.0f - bt) * (1.0f - bt) * (1.0f - bt));
                            } else {
                                const float bt = (bloomProg - 0.65f) / 0.35f;
                                revealScale = 1.25f - 0.15f * bt * bt;
                            }
                            revealX = 640.0f - cardW * revealScale * 0.5f;
                            revealY = 410.0f - cardH * revealScale * 0.5f;
                        } else {
                            // Dissipate: golden fade-out at center
                            boxAlpha = 0.0f;
                            revealScale = 1.1f;
                            revealX = 640.0f - cardW * revealScale * 0.5f;
                            revealY = 410.0f - cardH * revealScale * 0.5f;
                            const float dissipateProg = (rElapsed - 1.45f) / 0.3f;
                            cardAlpha = 1.0f - dissipateProg;
                            golden = true;
                        }
                    } else {
                        // Unselected: dim → shrink+sink+fade → gone
                        if (rElapsed < 0.2f) {
                            const float confirmProg = rElapsed / 0.2f;
                            cardAlpha = 1.0f - 0.35f * confirmProg;
                            boxAlpha = cardAlpha;
                        } else if (rElapsed < 0.55f) {
                            const float elimProg = (rElapsed - 0.2f) / 0.35f;
                            const float easeIn = elimProg * elimProg;
                            revealScale = 1.0f - 0.4f * easeIn;
                            cardAlpha = 0.65f * (1.0f - easeIn);
                            revealY = cy + 15.0f * easeIn;
                            boxAlpha = cardAlpha;
                        } else {
                            cardAlpha = 0.0f;
                            boxAlpha = 0.0f;
                        }
                    }
                }

                if (cardAlpha <= 0.0f) continue;

                const auto cardAlphaU8 = static_cast<std::uint8_t>(cardAlpha * 255.0f);
                const int texIdx = static_cast<int>(drawnCards_[i]);
                const bool isDealing = cardEventState_ == CardEventState::DealCards;

                // Deal animation (spin-scale)
                float dealScale = 1.0f;
                float dealRot = 0.0f;
                if (isDealing) {
                    const float dp = cardDealProgress_;
                    dealScale = 0.03f + (1.0f - 0.03f) * dp;
                    const float totalSpin = 720.0f + static_cast<float>(i) * 180.0f;
                    dealRot = totalSpin * (1.0f - dp) * (1.0f - dp);
                }

                // Flip: horizontal scale for 3D flip effect
                float flipX = 1.0f;
                const bool isFlipping = cardEventState_ == CardEventState::FlipCards;
                const bool afterDeal = cardEventState_ >= CardEventState::FlipCards;
                const bool showBack = isDealing || (isFlipping && cardFlipProgress_ < 0.5f);
                const bool showFace = afterDeal && !(isFlipping && cardFlipProgress_ < 0.5f);

                if (isFlipping) {
                    const float fp = cardFlipProgress_;
                    constexpr float kPi = 3.14159265f;
                    if (fp < 0.5f) {
                        flipX = std::cos(fp * 2.0f * kPi * 0.5f);  // 1 → 0
                    } else {
                        flipX = std::cos((1.0f - fp) * 2.0f * kPi * 0.5f);  // 0 → 1
                    }
                    if (flipX < 0.0f) flipX = 0.0f;
                }

                // Draw card back
                if (showBack && cardBackTex_.getSize().x > 0) {
                    sf::Sprite spr(cardBackTex_);
                    const float sx = cardScale * dealScale * flipX / static_cast<float>(cardBackTex_.getSize().x) * 73.0f;
                    const float sy = cardScale * dealScale / static_cast<float>(cardBackTex_.getSize().y) * 113.0f;
                    spr.setOrigin({cardBackTex_.getSize().x * 0.5f, cardBackTex_.getSize().y * 0.5f});
                    spr.setPosition({cx + cardW * 0.5f, cy + cardH * 0.5f});
                    spr.setScale({sx, sy});
                    spr.setRotation(sf::degrees(dealRot));
                    spr.setColor(sf::Color(255, 255, 255, cardAlphaU8));
                    window_.draw(spr);
                }

                // Draw card face
                if (showFace && cardTextures_[texIdx].getSize().x > 0) {
                    sf::Sprite spr(cardTextures_[texIdx]);
                    const float sx = cardScale * revealScale * flipX / static_cast<float>(cardTextures_[texIdx].getSize().x) * 73.0f;
                    const float sy = cardScale * revealScale / static_cast<float>(cardTextures_[texIdx].getSize().y) * 113.0f;
                    spr.setOrigin({cardTextures_[texIdx].getSize().x * 0.5f, cardTextures_[texIdx].getSize().y * 0.5f});
                    spr.setPosition({revealX + cardW * revealScale * 0.5f, revealY + cardH * revealScale * 0.5f});
                    spr.setScale({sx, sy});
                    spr.setRotation(sf::degrees(isDealing ? dealRot : 0.0f));
                    spr.setColor(sf::Color(255, 255, 255, cardAlphaU8));
                    window_.draw(spr);
                }

                // Golden glow ring around selected card during Confirm / Dissipate
                if (golden && cardAlpha > 0.0f) {
                    sf::RectangleShape glowRing;
                    const float gPad = 5.0f;
                    glowRing.setPosition({revealX - gPad, revealY - gPad});
                    glowRing.setSize({cardW * revealScale + gPad * 2.0f, cardH * revealScale + gPad * 2.0f});
                    glowRing.setFillColor(sf::Color::Transparent);
                    glowRing.setOutlineThickness(3.5f);
                    glowRing.setOutlineColor(sf::Color(255, 215, 40,
                        static_cast<std::uint8_t>(cardAlpha * 200.0f)));
                    window_.draw(glowRing);
                }

                // Info box (only after flip or during Choosing/Reveal)
                const bool showBox = cardEventState_ >= CardEventState::FlipCards;
                if (showBox && boxAlpha > 0.0f) {
                    sf::FloatRect cardBounds({cx, cy}, {cardW, cardH + boxGap + boxH});
                    const bool hovered = (cardEventState_ == CardEventState::Choosing) && cardBounds.contains(mousePos);

                    if (hovered && cardEventState_ == CardEventState::Choosing) {
                        sf::RectangleShape glow;
                        glow.setPosition({cx - 4.0f, cy - 4.0f});
                        glow.setSize({cardW + 8.0f, cardH + 8.0f});
                        glow.setFillColor(sf::Color::Transparent);
                        glow.setOutlineThickness(4.0f);
                        glow.setOutlineColor(sf::Color(245, 210, 100, 210));
                        window_.draw(glow);
                    }
                    drawInfoBox(cx, boxY, cardW, boxH, i, hovered, boxAlpha);
                }
            }
        }

        // Phase-specific overlay text
        if (cardEventState_ == CardEventState::MessengerWait) {
            const float pulse = std::sin(cardEventClock_.getElapsedTime().asSeconds() * 3.0f) * 0.3f + 0.7f;
            const auto promptColor = sf::Color(255, 245, 200,
                static_cast<std::uint8_t>(220.0f * pulse));
            if (client_) {
                drawCenteredText("等待房主开始转盘...",
                    sf::FloatRect({0.0f, 620.0f}, {1280.0f, 50.0f}), 24,
                    promptColor, sf::Text::Bold);
            } else {
                drawCenteredText("准备好请点击屏幕任意位置",
                    sf::FloatRect({0.0f, 620.0f}, {1280.0f, 50.0f}), 26,
                    promptColor, sf::Text::Bold);
            }
        }
        if (cardEventState_ == CardEventState::Choosing) {
            // Slide-in + glow pulse title at top-left
            const float t = std::min(1.0f, cardFloatTime_ / 0.5f);
            const float easeX = 1.0f - (1.0f - t) * (1.0f - t); // ease-out
            const float slideX = -300.0f + 300.0f * easeX;       // slide from left
            const float glow = std::sin(cardFloatTime_ * 2.5f) * 0.25f + 0.75f;
            const auto titleColor = sf::Color(
                static_cast<std::uint8_t>(245.0f),
                static_cast<std::uint8_t>(235.0f),
                static_cast<std::uint8_t>(210.0f),
                static_cast<std::uint8_t>(t * 255.0f));
            const auto glowColor = sf::Color(
                static_cast<std::uint8_t>(255.0f),
                static_cast<std::uint8_t>(245.0f),
                static_cast<std::uint8_t>(160.0f),
                static_cast<std::uint8_t>(t * glow * 100.0f));

            const std::string titleStr = "命运时刻 — 选择一张卡牌";
            const auto titleUtf8 = sf::String::fromUtf8(titleStr.begin(), titleStr.end());
            sf::Text titleText(uiFont_, titleUtf8, 28);
            titleText.setStyle(sf::Text::Bold);
            titleText.setPosition({40.0f + slideX, 28.0f});
            titleText.setFillColor(titleColor);

            // Glow behind text
            sf::Text glowText(uiFont_, titleUtf8, 28);
            glowText.setStyle(sf::Text::Bold);
            glowText.setFillColor(glowColor);
            glowText.setPosition({40.0f + slideX - 2.0f, 26.0f});
            window_.draw(glowText);
            window_.draw(titleText);

            // Decorative underline that slides in after title
            if (t > 0.6f) {
                const float lineT = (t - 0.6f) / 0.4f;
                sf::RectangleShape line;
                line.setPosition({40.0f, 62.0f});
                line.setSize({260.0f * lineT, 2.0f});
                line.setFillColor(sf::Color(245, 210, 100, static_cast<std::uint8_t>(lineT * 180.0f)));
                window_.draw(line);
            }
        }

        // --- Wheel Spin Screen (network mode: determines who picks) ---
        if (cardEventState_ == CardEventState::WheelSpin) {
            const float wElapsed = wheelSpinClock_.getElapsedTime().asSeconds();
            constexpr float kWheelDuration = 3.5f;
            const float wProg = std::min(1.0f, wElapsed / kWheelDuration);

            // Header
            {
                const std::string wheelTitle = "命运之轮";
                const auto titleUtf8 = sf::String::fromUtf8(wheelTitle.begin(), wheelTitle.end());
                sf::Text titleText(uiFont_, titleUtf8, 38);
                titleText.setStyle(sf::Text::Bold);
                const float pulse = std::sin(wElapsed * 2.5f) * 0.15f + 0.85f;
                titleText.setFillColor(sf::Color(255, 230, 140,
                    static_cast<std::uint8_t>(230.0f * pulse)));
                const auto tb = titleText.getLocalBounds();
                titleText.setPosition({(1280.0f - tb.size.x) * 0.5f, 60.0f});
                window_.draw(titleText);

                const std::string wheelSub = "决定由谁来抽取卡牌...";
                const auto subUtf8 = sf::String::fromUtf8(wheelSub.begin(), wheelSub.end());
                sf::Text subText(uiFont_, subUtf8, 18);
                subText.setFillColor(sf::Color(180, 170, 150, 200));
                const auto sb = subText.getLocalBounds();
                subText.setPosition({(1280.0f - sb.size.x) * 0.5f, 112.0f});
                window_.draw(subText);
            }

            // Draw the wheel
            constexpr float wheelRadius = 160.0f;
            constexpr float wheelCenterX = 640.0f;
            constexpr float wheelCenterY = 390.0f;

            // Rotation transform for the wheel — all elements rotate except the pointer
            sf::Transform wheelRot;
            wheelRot.rotate(sf::degrees(wheelAngle_), {wheelCenterX, wheelCenterY});
            const sf::RenderStates wheelRS(wheelRot);

            // Wheel rim glow (static — circular, rotation doesn't change appearance)
            {
                sf::CircleShape rim(wheelRadius + 12.0f);
                rim.setOrigin({wheelRadius + 12.0f, wheelRadius + 12.0f});
                rim.setPosition({wheelCenterX, wheelCenterY});
                rim.setFillColor(sf::Color::Transparent);
                rim.setOutlineThickness(6.0f);
                rim.setOutlineColor(sf::Color(180, 150, 80, 140));
                window_.draw(rim);
            }

            // Wheel body — two halves (rotated)
            {
                // Host half (0-180°)
                sf::CircleShape hostHalf(wheelRadius);
                hostHalf.setOrigin({wheelRadius, wheelRadius});
                hostHalf.setPosition({wheelCenterX, wheelCenterY});
                hostHalf.setFillColor(sf::Color(45, 60, 120, 220));
                hostHalf.setOutlineThickness(3.0f);
                hostHalf.setOutlineColor(sf::Color(180, 170, 120, 200));
                window_.draw(hostHalf, wheelRS);

                // Guest half (180-360°)
                sf::VertexArray guestHalf(sf::PrimitiveType::TriangleFan);
                guestHalf.append({{wheelCenterX, wheelCenterY}, sf::Color(120, 45, 40, 220)});
                constexpr int kSegments = 32;
                for (int s = kSegments / 2; s <= kSegments; ++s) {
                    float a = static_cast<float>(s) / static_cast<float>(kSegments) * 2.0f * 3.14159265f;
                    guestHalf.append({{wheelCenterX + std::cos(a) * wheelRadius,
                                       wheelCenterY + std::sin(a) * wheelRadius},
                                      sf::Color(120, 45, 40, 220)});
                }
                window_.draw(guestHalf, wheelRS);
            }

            // Tick marks (rotated — drawn relative to wheel, no manual angle offset)
            {
                constexpr int kTicks = 16;
                for (int t = 0; t < kTicks; ++t) {
                    float ta = static_cast<float>(t) / static_cast<float>(kTicks) * 2.0f * 3.14159265f;
                    float innerR = wheelRadius - 18.0f;
                    float outerR = wheelRadius - 4.0f;
                    sf::Vertex line[] = {
                        {{wheelCenterX + std::cos(ta) * innerR, wheelCenterY + std::sin(ta) * innerR},
                         sf::Color(220, 210, 160, 180)},
                        {{wheelCenterX + std::cos(ta) * outerR, wheelCenterY + std::sin(ta) * outerR},
                         sf::Color(220, 210, 160, 180)}
                    };
                    window_.draw(line, 2, sf::PrimitiveType::Lines, wheelRS);
                }
            }

            // Section labels (rotated with wheel)
            {
                // Host label at 90°
                const std::string hostLabel = "房主";
                const auto hlUtf8 = sf::String::fromUtf8(hostLabel.begin(), hostLabel.end());
                sf::Text hlText(uiFont_, hlUtf8, 22);
                hlText.setStyle(sf::Text::Bold);
                hlText.setFillColor(sf::Color(200, 210, 255, 240));
                const auto hlb = hlText.getLocalBounds();
                hlText.setPosition({wheelCenterX - hlb.size.x * 0.5f, wheelCenterY - 95.0f});
                window_.draw(hlText, wheelRS);

                // Guest label at 270°
                const std::string guestLabel = "访客";
                const auto glUtf8 = sf::String::fromUtf8(guestLabel.begin(), guestLabel.end());
                sf::Text glText(uiFont_, glUtf8, 22);
                glText.setStyle(sf::Text::Bold);
                glText.setFillColor(sf::Color(255, 200, 190, 240));
                const auto glb = glText.getLocalBounds();
                glText.setPosition({wheelCenterX - glb.size.x * 0.5f, wheelCenterY + 72.0f});
                window_.draw(glText, wheelRS);
            }

            // Center hub (rotated)
            {
                sf::CircleShape hub(14.0f);
                hub.setOrigin({14.0f, 14.0f});
                hub.setPosition({wheelCenterX, wheelCenterY});
                hub.setFillColor(sf::Color(200, 180, 100, 240));
                hub.setOutlineThickness(2.0f);
                hub.setOutlineColor(sf::Color(140, 120, 60, 200));
                window_.draw(hub, wheelRS);
            }

            // Pointer triangle at the top (fixed — does not rotate)
            {
                sf::VertexArray pointer(sf::PrimitiveType::Triangles);
                pointer.append({{wheelCenterX, wheelCenterY - wheelRadius + 6.0f},
                                sf::Color(255, 220, 60, 240)});
                pointer.append({{wheelCenterX - 14.0f, wheelCenterY - wheelRadius - 18.0f},
                                sf::Color(255, 220, 60, 240)});
                pointer.append({{wheelCenterX + 14.0f, wheelCenterY - wheelRadius - 18.0f},
                                sf::Color(255, 220, 60, 240)});
                window_.draw(pointer);

                // Pointer pulse effect while spinning
                if (wProg < 1.0f) {
                    sf::CircleShape ptrGlow(8.0f);
                    ptrGlow.setOrigin({8.0f, 8.0f});
                    ptrGlow.setPosition({wheelCenterX, wheelCenterY - wheelRadius - 4.0f});
                    const float ptrPulse = std::sin(wElapsed * 8.0f) * 0.4f + 0.6f;
                    ptrGlow.setFillColor(sf::Color(255, 240, 100,
                        static_cast<std::uint8_t>(160.0f * ptrPulse)));
                    window_.draw(ptrGlow);
                }
            }

            // Result text after wheel stops
            if (wheelStopped_) {
                const float resultElapsed = wElapsed - kWheelDuration;
                const float fadeIn = std::min(1.0f, resultElapsed / 0.5f);

                if (wheelResultSet_) {
                    std::string resultStr;
                    if (iAmPicker_) {
                        resultStr = "你来抽卡！";
                    } else if (wheelResultHost_) {
                        resultStr = "房主抽卡";
                    } else {
                        resultStr = "访客抽卡";
                    }
                    const auto resultUtf8 = sf::String::fromUtf8(resultStr.begin(), resultStr.end());
                    sf::Text resultText(uiFont_, resultUtf8, 32);
                    resultText.setStyle(sf::Text::Bold);
                    resultText.setFillColor(sf::Color(255, 230, 140,
                        static_cast<std::uint8_t>(240.0f * fadeIn)));
                    const auto rb = resultText.getLocalBounds();
                    resultText.setPosition({(1280.0f - rb.size.x) * 0.5f, 590.0f});
                    window_.draw(resultText);

                    // Countdown to cards
                    const float cardsIn = std::max(0.0f, 2.0f - resultElapsed);
                    const std::string countStr = std::to_string(static_cast<int>(cardsIn + 0.99f)) + " 秒后抽卡...";
                    const auto countUtf8 = sf::String::fromUtf8(countStr.begin(), countStr.end());
                    sf::Text countText(uiFont_, countUtf8, 16);
                    countText.setFillColor(sf::Color(180, 170, 150,
                        static_cast<std::uint8_t>(180.0f * fadeIn)));
                    const auto cb = countText.getLocalBounds();
                    countText.setPosition({(1280.0f - cb.size.x) * 0.5f, 640.0f});
                    window_.draw(countText);
                } else {
                    // Client waiting for server's result
                    const std::string waitStr = "等待结果...";
                    const auto waitUtf8 = sf::String::fromUtf8(waitStr.begin(), waitStr.end());
                    sf::Text waitText(uiFont_, waitUtf8, 24);
                    const float waitPulse = std::sin(wElapsed * 4.0f) * 0.3f + 0.7f;
                    waitText.setFillColor(sf::Color(200, 190, 160,
                        static_cast<std::uint8_t>(200.0f * waitPulse)));
                    const auto wb = waitText.getLocalBounds();
                    waitText.setPosition({(1280.0f - wb.size.x) * 0.5f, 600.0f});
                    window_.draw(waitText);
                }
            }
        }
    }

    if (networkDisconnected_) {
        // Dark overlay mask
        sf::RectangleShape overlay;
        overlay.setPosition({0.0f, 0.0f});
        overlay.setSize({1280.0f, 820.0f});
        overlay.setFillColor(sf::Color(8, 5, 18, 200));
        window_.draw(overlay);

        // Transparent frosted glass dialog
        const float dlgX = 290.0f;
        const float dlgY = 270.0f;
        const float dlgW = 700.0f;
        const float dlgH = 220.0f;

        // Drop shadow
        sf::RectangleShape dlgShadow;
        dlgShadow.setPosition({dlgX + 4.0f, dlgY + 6.0f});
        dlgShadow.setSize({dlgW, dlgH});
        dlgShadow.setFillColor(sf::Color(6, 4, 14, 120));
        window_.draw(dlgShadow);

        // Main glass background — transparent
        sf::RectangleShape dlgBg;
        dlgBg.setPosition({dlgX, dlgY});
        dlgBg.setSize({dlgW, dlgH});
        dlgBg.setFillColor(sf::Color(14, 8, 22, 145));
        dlgBg.setOutlineThickness(1.0f);
        dlgBg.setOutlineColor(sf::Color(120, 95, 160, 60));
        window_.draw(dlgBg);

        // Top highlight bevel
        sf::RectangleShape dlgHighlight;
        dlgHighlight.setPosition({dlgX + 2.0f, dlgY + 1.0f});
        dlgHighlight.setSize({dlgW - 4.0f, 1.0f});
        dlgHighlight.setFillColor(sf::Color(160, 135, 200, 40));
        window_.draw(dlgHighlight);

        // Bottom shadow line
        sf::RectangleShape dlgBotShadow;
        dlgBotShadow.setPosition({dlgX + 2.0f, dlgY + dlgH - 1.0f});
        dlgBotShadow.setSize({dlgW - 4.0f, 1.0f});
        dlgBotShadow.setFillColor(sf::Color(8, 4, 14, 60));
        window_.draw(dlgBotShadow);

        // Noise texture overlay for glass effect
        if (noiseTex_.getSize().x > 0) {
            sf::Sprite noiseOverlay(noiseTex_);
            noiseOverlay.setPosition({dlgX, dlgY});
            const auto nSize = noiseTex_.getSize();
            noiseOverlay.setScale({dlgW / static_cast<float>(nSize.x),
                                   dlgH / static_cast<float>(nSize.y)});
            noiseOverlay.setColor(sf::Color(255, 255, 255, 14));
            window_.draw(noiseOverlay);
        }

        // Message text
        drawCenteredText("另一位玩家已经退出游戏对局",
                         sf::FloatRect({dlgX, dlgY + 42.0f}, {dlgW, 40.0f}), 26,
                         sf::Color(235, 225, 210), sf::Text::Bold);

        // Frosted glass button with press animation
        const float btnW = 360.0f;
        const float btnH = 64.0f;
        const float btnX = dlgX + (dlgW - btnW) * 0.5f;
        const float btnY = dlgY + 118.0f;
        const sf::Vector2f mousePosition = static_cast<sf::Vector2f>(sf::Mouse::getPosition(window_));
        const bool btnPressed = buttonAnimIndex_ == 310;
        const bool btnHovered = !btnPressed && sf::FloatRect({btnX, btnY}, {btnW, btnH}).contains(mousePosition);
        const float btnScale = btnPressed ? 0.92f : 1.0f;
        const float bW = btnW * btnScale;
        const float bH = btnH * btnScale;
        const float bX = btnX + (btnW - bW) * 0.5f;
        const float bY = btnY + (btnH - bH) * 0.5f;

        // Button shadow
        sf::RectangleShape btnShadow;
        btnShadow.setPosition({bX + 2.0f, bY + 3.0f});
        btnShadow.setSize({bW, bH});
        btnShadow.setFillColor(sf::Color(6, 4, 14, btnHovered ? 130 : 90));
        window_.draw(btnShadow);

        // Button glass fill
        sf::RectangleShape btnFill;
        btnFill.setPosition({bX, bY});
        btnFill.setSize({bW, bH});
        btnFill.setFillColor(btnHovered
            ? sf::Color(35, 22, 50, 220)
            : sf::Color(25, 16, 35, 190));
        btnFill.setOutlineThickness(1.5f);
        btnFill.setOutlineColor(btnHovered
            ? sf::Color(120, 90, 160, 100)
            : sf::Color(85, 65, 115, 80));
        window_.draw(btnFill);

        // Button top bevel
        sf::RectangleShape btnBevel;
        btnBevel.setPosition({bX + 2.0f, bY + 1.0f});
        btnBevel.setSize({bW - 4.0f, 1.0f});
        btnBevel.setFillColor(sf::Color(140, 115, 180, btnHovered ? 60 : 40));
        window_.draw(btnBevel);

        // Button noise
        if (noiseTex_.getSize().x > 0) {
            sf::Sprite btnNoise(noiseTex_);
            btnNoise.setPosition({bX, bY});
            const auto nSize = noiseTex_.getSize();
            btnNoise.setScale({bW / static_cast<float>(nSize.x),
                               bH / static_cast<float>(nSize.y)});
            btnNoise.setColor(sf::Color(255, 255, 255, btnHovered ? 22 : 14));
            window_.draw(btnNoise);
        }

        // Button hover glow
        if (btnHovered) {
            sf::RectangleShape btnGlow;
            btnGlow.setPosition({bX, bY});
            btnGlow.setSize({bW, bH});
            btnGlow.setFillColor(sf::Color(255, 255, 220, 12));
            window_.draw(btnGlow);
        }

        drawCenteredText("返回主菜单",
                         sf::FloatRect({bX, bY}, {bW, bH}), static_cast<unsigned>(24.0f * btnScale),
                         btnHovered ? sf::Color(255, 250, 235, 250) : sf::Color(220, 210, 190, 220),
                         sf::Text::Bold);
    }

    // Restore view after shake
    if (shakeIntensity_ > 0.0f && (cardEventState_ == CardEventState::Omen || deathAnimPending_ || loversAnimPending_ || worldAnimPending_ || empressAnimPending_ || highPriestessAnimPending_ || sunAnimPending_ || moonAnimPending_ || hermitAnimPending_ || hierophantAnimPending_ || justiceAnimPending_ || hangedManAnimPending_ || devilAnimPending_ || chariotAnimPending_ || magicianAnimPending_ || temperanceAnimPending_ || foolAnimPending_ || towerAnimPending_ || wheelFortuneAnimPending_ || judgementAnimPending_ || (starAnimPending_ && !starInPersistentMode_))) {
        window_.setView(savedView);
    }
}

void Game::drawButton(const UiButton& button, bool hovered, unsigned int characterSize, bool pressed) {
    const auto& b = button.bounds;
    const float scale = pressed ? 0.92f : 1.0f;
    const float sx = b.size.x * scale;
    const float sy = b.size.y * scale;
    const float ox = b.position.x + (b.size.x - sx) * 0.5f;
    const float oy = b.position.y + (b.size.y - sy) * 0.5f;

    // Shadow
    sf::RectangleShape shadow;
    shadow.setPosition({ox + 2.0f, oy + 3.0f});
    shadow.setSize({sx, sy});
    shadow.setFillColor(sf::Color(12, 10, 18, hovered ? 100 : 70));
    window_.draw(shadow);

    // Semi-transparent fill
    sf::RectangleShape fill;
    fill.setPosition({ox, oy});
    fill.setSize({sx, sy});
    fill.setFillColor(hovered
        ? sf::Color(button.fill.r, button.fill.g, button.fill.b, std::min(255, button.fill.a + 35))
        : button.fill);
    window_.draw(fill);

    // Border
    sf::RectangleShape border;
    border.setPosition({ox, oy});
    border.setSize({sx, sy});
    border.setFillColor(sf::Color::Transparent);
    border.setOutlineThickness(hovered ? 3.0f : 2.0f);
    border.setOutlineColor(hovered
        ? sf::Color(button.accent.r, button.accent.g, button.accent.b, std::min(255, button.accent.a + 30))
        : button.accent);
    window_.draw(border);

    // Hover glow
    if (hovered) {
        sf::RectangleShape glow;
        glow.setPosition({ox, oy});
        glow.setSize({sx, sy});
        glow.setFillColor(sf::Color(255, 255, 230, 16));
        window_.draw(glow);
    }

    const sf::FloatRect scaledBounds({ox, oy}, {sx, sy});
    drawCenteredText(button.label, scaledBounds, characterSize,
                     sf::Color(252, 248, 238, 240), sf::Text::Bold);
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
    if (networkMode_ && gameOver_ && winner_ == Board::Piece::Black) {
        return isNetworkHost_ ? "你赢了！" : "对手获胜。";
    }
    if (networkMode_ && gameOver_ && winner_ == Board::Piece::White) {
        return isNetworkHost_ ? "对手获胜。" : "你赢了！";
    }
    if (networkMode_ && gameOver_) {
        return "棋盘已满，本局平局。";
    }
    if (networkMode_ && isMyTurn()) {
        return "你的回合，请落子。";
    }
    if (networkMode_ && !isMyTurn()) {
        return "等待对手落子...";
    }
    if (winner_ == Board::Piece::Black) {
        return "黑棋获胜。";
    }
    if (winner_ == Board::Piece::White) {
        return "白棋获胜。";
    }
    if (gameOver_) {
        return "棋盘已满，本局平局。";
    }
    if (obstacleEventPending_) {
        return "障碍物变化中...";
    }
    if (aiEnabled_ && currentTurn_ == Board::Piece::White) {
        return "AI 正在计算下一步。";
    }
    return pieceName(currentTurn_) + "回合，请落子。";
}

std::string Game::subtitleLine() const {
    if (networkMode_) {
        auto line = modeName() + " | 网络对战";
        line += isNetworkHost_ ? " | 你执黑先手" : " | 你执白后手";
        return line;
    }
    auto line = modeName() + " | " + playerModeName();
    if (aiEnabled_) {
        line += " | " + difficultyName() + "难度";
    }
    return line;
}

void Game::startCrossfade(sf::Music* target, float targetVol) {
    // Set up crossfade: current music fades out, target fades in
    outgoingMusic_ = currentMusic_;
    outgoingStartVol_ = currentMusicVol_;
    currentMusic_ = target;
    currentMusicVol_ = targetVol;

    if (outgoingMusic_ && outgoingMusic_->getStatus() != sf::Music::Status::Playing) {
        outgoingMusic_ = nullptr; // Nothing to fade out
    }
    if (currentMusic_) {
        currentMusic_->setVolume(0.0f);
        currentMusic_->play();
    }
    crossfadeClock_.restart();
}

void Game::playPlaceSound() {
    placeSfx_.stop();
    placeSfx_.play();
}

void Game::playButtonSound() {
    if (btnSound_.has_value()) {
        btnSound_->play();
    }
}

void Game::updateWindowTitle() {
    switch (scene_) {
    case Scene::MainMenu:
        window_.setTitle(sf::String::fromUtf8("幻格五子棋 - 主菜单",
                                              "幻格五子棋 - 主菜单" + std::char_traits<char>::length("幻格五子棋 - 主菜单")));
        return;
    case Scene::Rules:
        window_.setTitle(sf::String::fromUtf8("幻格五子棋 - 操作规则说明",
                                              "幻格五子棋 - 操作规则说明" + std::char_traits<char>::length("幻格五子棋 - 操作规则说明")));
        return;
    case Scene::ModeSelect:
        window_.setTitle(sf::String::fromUtf8("幻格五子棋 - 模式选择",
                                              "幻格五子棋 - 模式选择" + std::char_traits<char>::length("幻格五子棋 - 模式选择")));
        return;
    case Scene::DifficultySelect:
        window_.setTitle(sf::String::fromUtf8("幻格五子棋 - 选择AI难度",
                                              "幻格五子棋 - 选择AI难度" + std::char_traits<char>::length("幻格五子棋 - 选择AI难度")));
        return;
    case Scene::NetworkLobby:
        window_.setTitle(sf::String::fromUtf8("幻格五子棋 - 局域网联机",
                                              "幻格五子棋 - 局域网联机" + std::char_traits<char>::length("幻格五子棋 - 局域网联机")));
        return;
    case Scene::RoomSetup:
        window_.setTitle(sf::String::fromUtf8("幻格五子棋 - 房间设置",
                                              "幻格五子棋 - 房间设置" + std::char_traits<char>::length("幻格五子棋 - 房间设置")));
        return;
    case Scene::NetworkWait:
        window_.setTitle(sf::String::fromUtf8("幻格五子棋 - 等待对手",
                                              "幻格五子棋 - 等待对手" + std::char_traits<char>::length("幻格五子棋 - 等待对手")));
        return;
    case Scene::Playing:
        break;
    }

    std::string title = "幻格五子棋 - " + modeName() + " - ";
    if (networkMode_) {
        title += "网络对战";
    } else {
        title += playerModeName();
        if (aiEnabled_) {
            title += " (" + difficultyName() + ")";
        }
    }
    title += " - ";
    if (winner_ != Board::Piece::None) {
        title += pieceName(winner_) + "胜";
    } else if (gameOver_) {
        title += "平局";
    } else {
        title += pieceName(currentTurn_) + "回合";
    }
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

// ======== Card Event System ========

// ======== Heart Texture Generation (Lovers card) ========

void Game::generateHeartTextures() {
    if (heartTexturesLoaded_) return;
    heartTexturesLoaded_ = true;

    // Heart curve: (x²+y²-1)³ - x²y³ ≤ 0
    auto heartDist = [](float x, float y) -> float {
        const float xx = x * x;
        const float yy = y * y;
        const float t1 = xx + yy - 1.0f;
        return t1 * t1 * t1 - xx * yy * y;
    };

    // --- Heart Glow (64x64) — soft heart-shaped glow with radial falloff ---
    {
        constexpr int hs = 64;
        sf::Image img({hs, hs}, sf::Color::Transparent);
        for (int py = 0; py < hs; ++py) {
            for (int px = 0; px < hs; ++px) {
                const float x = (static_cast<float>(px) / 31.5f - 1.0f) * 1.2f;
                const float y = -(static_cast<float>(py) / 31.5f - 1.0f) * 1.2f + 0.25f;
                const float d = heartDist(x, y);
                float alpha;
                int r, g, b;
                if (d <= 0.0f) {
                    // Inside heart: bright pink-white core
                    const float depth = std::min(1.0f, -d / 0.4f);
                    alpha = 0.55f + depth * 0.45f;
                    r = 255;
                    g = static_cast<int>(118.0f + depth * 90.0f);
                    b = static_cast<int>(140.0f + depth * 70.0f);
                } else {
                    // Outside: glow fading exponentially
                    const float glow = std::exp(-d * 4.5f);
                    if (glow < 0.01f) continue;
                    alpha = glow * 0.5f;
                    r = 255;
                    g = static_cast<int>(100.0f + glow * 60.0f);
                    b = static_cast<int>(125.0f + glow * 45.0f);
                }
                r = std::clamp(r, 0, 255);
                g = std::clamp(g, 0, 255);
                b = std::clamp(b, 0, 255);
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
                if (a == 0) continue;
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(heartGlowTex_.loadFromImage(img));
        heartGlowTex_.setSmooth(true);
    }

    // --- Heart Small (16x16) — crisp pixel heart for particles ---
    {
        constexpr int ss = 16;
        sf::Image img({ss, ss}, sf::Color::Transparent);
        for (int py = 0; py < ss; ++py) {
            for (int px = 0; px < ss; ++px) {
                const float x = (static_cast<float>(px) / 7.5f - 1.0f) * 1.08f;
                const float y = -(static_cast<float>(py) / 7.5f - 1.0f) * 1.08f + 0.22f;
                const float d = heartDist(x, y);
                if (d > 0.05f) continue; // outside heart
                const float sharpness = std::min(1.0f, (0.05f - d) / 0.12f);
                const auto a = static_cast<std::uint8_t>(sharpness * 255.0f);
                if (a == 0) continue;
                // Brighter toward center
                const int brt = static_cast<int>(210.0f + sharpness * 45.0f);
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(brt),
                                       static_cast<std::uint8_t>(105.0f + sharpness * 50.0f),
                                       static_cast<std::uint8_t>(130.0f + sharpness * 40.0f), a));
            }
        }
        static_cast<void>(heartSmallTex_.loadFromImage(img));
        heartSmallTex_.setSmooth(false);
    }

    // --- Heart Burst (96x96) — large radiant heart with layered glow rings ---
    {
        constexpr int bs = 96;
        sf::Image img({bs, bs}, sf::Color::Transparent);
        for (int py = 0; py < bs; ++py) {
            for (int px = 0; px < bs; ++px) {
                const float x = (static_cast<float>(px) / 47.5f - 1.0f) * 1.15f;
                const float y = -(static_cast<float>(py) / 47.5f - 1.0f) * 1.15f + 0.25f;
                const float d = heartDist(x, y);
                float alpha;
                int r = 255, g = 90, b = 115;
                if (d <= -0.05f) {
                    // Inner heart: bright core
                    const float depth = std::min(1.0f, (-d - 0.05f) / 0.5f);
                    alpha = 0.8f + depth * 0.2f;
                    g = static_cast<int>(130.0f + depth * 100.0f);
                    b = static_cast<int>(150.0f + depth * 80.0f);
                } else if (d <= 0.0f) {
                    // Heart edge
                    const float edgeFade = (0.0f - d) / 0.05f;
                    alpha = 0.5f + edgeFade * 0.3f;
                } else {
                    // Glow rings: multiple exponentials for layered look
                    const float g1 = std::exp(-d * 3.0f) * 0.35f;
                    const float g2 = std::exp(-d * 8.0f) * 0.25f;
                    const float g3 = std::exp(-d * 18.0f) * 0.15f;
                    alpha = g1 + g2 + g3;
                    if (alpha < 0.015f) continue;
                    r = 255;
                    g = static_cast<int>(75.0f + alpha * 80.0f);
                    b = static_cast<int>(100.0f + alpha * 70.0f);
                }
                r = std::clamp(r, 0, 255);
                g = std::clamp(g, 0, 255);
                b = std::clamp(b, 0, 255);
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
                if (a == 0) continue;
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(heartBurstTex_.loadFromImage(img));
        heartBurstTex_.setSmooth(true);
    }
}

void Game::generateWorldTextures() {
    if (worldTexturesLoaded_) return;
    worldTexturesLoaded_ = true;

    // --- Mandala Gear (128x128) — golden pixel-art gear/mandala ---
    {
        constexpr int ms = 128;
        constexpr float half = 64.0f;
        sf::Image img({ms, ms}, sf::Color::Transparent);
        for (int py = 0; py < ms; ++py) {
            for (int px = 0; px < ms; ++px) {
                const float dx = static_cast<float>(px) - half + 0.5f;
                const float dy = static_cast<float>(py) - half + 0.5f;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float angle = std::atan2(dy, dx);

                constexpr int kTeeth = 8;
                constexpr float kToothHeight = 12.0f;
                constexpr float kBaseRadius = 22.0f;
                const float toothPhase = std::fmod(angle + 3.14159265f / static_cast<float>(kTeeth),
                                                    2.0f * 3.14159265f / static_cast<float>(kTeeth));
                const float toothNorm = toothPhase / (2.0f * 3.14159265f / static_cast<float>(kTeeth));
                float toothFactor;
                if (toothNorm < 0.18f || toothNorm > 0.82f) {
                    toothFactor = 0.0f;
                } else {
                    const float t = (toothNorm - 0.18f) / 0.64f;
                    toothFactor = std::sin(t * 3.14159265f);
                }
                const float gearEdge = kBaseRadius + toothFactor * kToothHeight;
                const float ringRidge1 = 30.0f + std::cos(angle * 24.0f) * 1.5f;
                const float ringRidge2 = 40.0f + std::cos(angle * 16.0f + 1.2f) * 2.0f;

                int r = 0, g = 0, b = 0;
                float alpha = 0.0f;

                if (dist <= 6.0f) {
                    const float coreGrad = dist / 6.0f;
                    alpha = 1.0f;
                    r = 255;
                    g = static_cast<int>(240.0f - coreGrad * 40.0f);
                    b = static_cast<int>(180.0f - coreGrad * 80.0f);
                } else if (dist <= gearEdge && dist <= ringRidge1) {
                    const float innerFade = (dist - 6.0f) / 8.0f;
                    alpha = 0.85f + innerFade * 0.15f;
                    r = static_cast<int>(220.0f - innerFade * 40.0f);
                    g = static_cast<int>(170.0f - innerFade * 30.0f);
                    b = static_cast<int>(80.0f - innerFade * 15.0f);
                    if (toothFactor > 0.7f && dist > kBaseRadius) {
                        const float peakGlow = (toothFactor - 0.7f) / 0.3f;
                        r = std::min(255, r + static_cast<int>(peakGlow * 45.0f));
                        g = std::min(255, g + static_cast<int>(peakGlow * 50.0f));
                    }
                } else if (dist <= ringRidge1 + 2.5f) {
                    alpha = 0.55f;
                    r = 140; g = 90; b = 30;
                } else if (dist <= ringRidge2) {
                    const float mid = (dist - ringRidge1 - 2.5f) / (ringRidge2 - ringRidge1 - 2.5f);
                    alpha = 0.4f + mid * 0.3f;
                    r = static_cast<int>(180.0f + mid * 10.0f);
                    g = static_cast<int>(120.0f + mid * 15.0f);
                    b = static_cast<int>(45.0f + mid * 10.0f);
                } else if (dist <= ringRidge2 + 2.0f) {
                    alpha = 0.5f;
                    r = 130; g = 75; b = 25;
                } else if (dist <= 50.0f) {
                    const float outerFade = (dist - ringRidge2 - 2.0f) / (50.0f - ringRidge2 - 2.0f);
                    alpha = 0.35f * (1.0f - outerFade);
                    r = static_cast<int>(160.0f * (1.0f - outerFade));
                    g = static_cast<int>(100.0f * (1.0f - outerFade));
                    b = static_cast<int>(35.0f * (1.0f - outerFade));
                } else if (dist <= 62.0f) {
                    const float glowFade = (dist - 50.0f) / 12.0f;
                    alpha = 0.12f * (1.0f - glowFade);
                    r = 220; g = 160; b = 65;
                }
                if (dist > ringRidge1 - 1.0f && dist < ringRidge1 + 1.5f &&
                    std::abs(std::fmod(angle + 0.15f, 2.0f * 3.14159265f / 8.0f)
                             - 3.14159265f / 16.0f) < 0.08f) {
                    alpha = std::max(alpha, 0.7f);
                    r = 255; g = 220; b = 130;
                }

                r = std::clamp(r, 0, 255);
                g = std::clamp(g, 0, 255);
                b = std::clamp(b, 0, 255);
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
                if (a == 0) continue;
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(mandalaTex_.loadFromImage(img));
        mandalaTex_.setSmooth(false);
    }

    // --- Gold Spark (16x16) — pixel diamond sparkle ---
    {
        constexpr int ss = 16;
        sf::Image img({ss, ss}, sf::Color::Transparent);
        for (int py = 0; py < ss; ++py) {
            for (int px = 0; px < ss; ++px) {
                constexpr float cx = 7.5f, cy = 7.5f;
                const float dx = std::abs(static_cast<float>(px) - cx);
                const float dy = std::abs(static_cast<float>(py) - cy);
                const float diamondDist = dx + dy;
                float alpha = 0.0f;
                int r = 255, g = 220, b = 100;
                if (diamondDist < 3.0f) {
                    alpha = 1.0f;
                    r = 255; g = 250; b = 200;
                } else if (diamondDist < 5.5f) {
                    const float t = (diamondDist - 3.0f) / 2.5f;
                    alpha = 1.0f - t * 0.4f;
                    r = 255;
                    g = static_cast<int>(220.0f - t * 50.0f);
                    b = static_cast<int>(140.0f - t * 50.0f);
                } else if (diamondDist < 7.5f) {
                    const float t = (diamondDist - 5.5f) / 2.0f;
                    alpha = 0.6f * (1.0f - t);
                    r = static_cast<int>(220.0f - t * 40.0f);
                    g = static_cast<int>(150.0f - t * 40.0f);
                    b = static_cast<int>(70.0f - t * 30.0f);
                }
                r = std::clamp(r, 0, 255);
                g = std::clamp(g, 0, 255);
                b = std::clamp(b, 0, 255);
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
                if (a == 0) continue;
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(goldSparkTex_.loadFromImage(img));
        goldSparkTex_.setSmooth(false);
    }
}

void Game::generateStrengthTextures() {
    if (strengthTexturesLoaded_) return;
    strengthTexturesLoaded_ = true;

    // --- Strength Rune (64x64) — golden cracked-energy glyph ---
    {
        constexpr int rs = 64;
        constexpr float half = 32.0f;
        sf::Image img({rs, rs}, sf::Color::Transparent);
        for (int py = 0; py < rs; ++py) {
            for (int px = 0; px < rs; ++px) {
                const float dx = static_cast<float>(px) - half + 0.5f;
                const float dy = static_cast<float>(py) - half + 0.5f;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float angle = std::atan2(dy, dx);

                // Generate pseudo-random crack lines based on angle + distance
                const float crackSeed1 = std::sin(angle * 5.0f + 1.3f) * std::cos(angle * 3.0f - 0.7f);
                const float crackSeed2 = std::sin(angle * 7.0f - 2.1f) * std::cos(dist * 0.3f);
                const float crackVal = std::abs(crackSeed1 * crackSeed2);

                float alpha = 0.0f;
                int r = 255, g = 200, b = 60;

                if (dist < 5.0f) {
                    // Core: bright golden-white
                    alpha = 1.0f - dist / 5.0f * 0.3f;
                    r = 255; g = 240; b = 160;
                } else if (crackVal > 0.55f && dist < 28.0f) {
                    // Crack lines: bright golden veins
                    const float crackStrength = (crackVal - 0.55f) / 0.45f;
                    const float distFade = 1.0f - dist / 28.0f;
                    alpha = crackStrength * 0.85f * distFade;
                    r = 255;
                    g = static_cast<int>(180.0f + crackStrength * 60.0f);
                    b = static_cast<int>(40.0f + crackStrength * 60.0f);
                } else if (dist < 20.0f && crackVal > 0.35f) {
                    // Secondary cracks: fainter
                    const float crackStrength = (crackVal - 0.35f) / 0.2f;
                    alpha = crackStrength * 0.35f * (1.0f - dist / 20.0f);
                    r = 240; g = 160; b = 30;
                } else if (dist < 30.0f) {
                    // Subtle amber glow between cracks
                    const float glow = (1.0f - dist / 30.0f) * 0.08f;
                    alpha = glow;
                    r = 220; g = 140; b = 35;
                }

                r = std::clamp(r, 0, 255);
                g = std::clamp(g, 0, 255);
                b = std::clamp(b, 0, 255);
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
                if (a == 0) continue;
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(strengthRuneTex_.loadFromImage(img));
        strengthRuneTex_.setSmooth(false);
    }

    // --- Strength Spark (12x12) — sharp golden particle ---
    {
        constexpr int ss = 12;
        constexpr float cx = 5.5f, cy = 5.5f;
        sf::Image img({ss, ss}, sf::Color::Transparent);
        for (int py = 0; py < ss; ++py) {
            for (int px = 0; px < ss; ++px) {
                const float dx = std::abs(static_cast<float>(px) - cx);
                const float dy = std::abs(static_cast<float>(py) - cy);
                const float diamond = dx + dy;

                float alpha = 0.0f;
                int r = 255, g = 220, b = 80;
                if (diamond < 1.5f) {
                    alpha = 1.0f;
                    r = 255; g = 250; b = 210;
                } else if (diamond < 3.5f) {
                    const float t = (diamond - 1.5f) / 2.0f;
                    alpha = 1.0f - t * 0.5f;
                    r = 255;
                    g = static_cast<int>(230.0f - t * 60.0f);
                    b = static_cast<int>(150.0f - t * 70.0f);
                } else if (diamond < 5.5f) {
                    const float t = (diamond - 3.5f) / 2.0f;
                    alpha = 0.5f * (1.0f - t);
                    r = 240; g = 160; b = 50;
                }
                r = std::clamp(r, 0, 255);
                g = std::clamp(g, 0, 255);
                b = std::clamp(b, 0, 255);
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
                if (a == 0) continue;
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(strengthSparkTex_.loadFromImage(img));
        strengthSparkTex_.setSmooth(false);
    }
}

void Game::generateEmpressTextures() {
    if (empressTexturesLoaded_) return;
    empressTexturesLoaded_ = true;

    // --- Petal texture (24x32) — elongated rounded petal, pink/rose gradient ---
    {
        constexpr int pw = 24;
        constexpr int ph = 32;
        constexpr float cx = 11.5f, cy = 22.0f;
        sf::Image img({pw, ph}, sf::Color::Transparent);
        for (int py = 0; py < ph; ++py) {
            for (int px = 0; px < pw; ++px) {
                const float dx = static_cast<float>(px) - cx;
                const float dy = static_cast<float>(py) - cy;
                const float ny = -(dy / 28.0f);
                const float ndx = dx / (8.0f + ny * 5.0f);
                const float dist = std::sqrt(ndx * ndx + ny * ny);
                const float shape = 1.0f - std::min(dist, 1.0f);
                if (shape < 0.02f) continue;
                const float edge = 1.0f - dist;
                const float alpha = std::pow(shape, 1.5f) * (0.7f + 0.3f * edge);
                const float tipness = (ny + 1.0f) * 0.5f;
                int r = static_cast<int>(240.0f + tipness * 15.0f);
                int g = static_cast<int>(110.0f + tipness * 75.0f);
                int b = static_cast<int>(130.0f + tipness * 55.0f);
                r = std::min(255, r + static_cast<int>(edge * 20.0f));
                g = std::min(255, g + static_cast<int>(edge * 30.0f));
                b = std::min(255, b + static_cast<int>(edge * 30.0f));
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(petalTex_.loadFromImage(img));
        petalTex_.setSmooth(true);
    }

    // --- Bloom glow texture (64x64) — warm golden radial gradient ---
    {
        constexpr int gs = 64;
        constexpr float ghalf = 31.5f;
        sf::Image img({gs, gs}, sf::Color::Transparent);
        for (int py = 0; py < gs; ++py) {
            for (int px = 0; px < gs; ++px) {
                const float dx = static_cast<float>(px) - ghalf;
                const float dy = static_cast<float>(py) - ghalf;
                const float dist = std::sqrt(dx * dx + dy * dy) / 32.0f;
                if (dist >= 1.0f) continue;
                const float alpha = std::pow(1.0f - dist, 2.2f) * 0.9f;
                const float core = std::max(0.0f, 1.0f - dist * 4.0f);
                const int r = 255;
                const int g = static_cast<int>(210.0f + core * 45.0f - dist * 50.0f);
                const int b = static_cast<int>(100.0f + core * 155.0f - dist * 60.0f);
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(std::clamp(g, 0, 255)),
                                       static_cast<std::uint8_t>(std::clamp(b, 0, 255)), a));
            }
        }
        static_cast<void>(bloomGlowTex_.loadFromImage(img));
        bloomGlowTex_.setSmooth(true);
    }
}

void Game::generateHighPriestessTextures() {
    if (hpTexturesLoaded_) return;
    hpTexturesLoaded_ = true;

    // --- Pillar glow texture (32x128) — vertical gradient, ice-blue to white ---
    {
        constexpr int pw = 32;
        constexpr int ph = 128;
        sf::Image img({pw, ph}, sf::Color::Transparent);
        for (int py = 0; py < ph; ++py) {
            const float ny = static_cast<float>(py) / static_cast<float>(ph); // 0=top, 1=bottom
            // Double falloff: brightest at both ends (symmetric), dim in middle
            const float edgeDist = 1.0f - std::abs(ny - 0.5f) * 2.0f; // 0 at middle, 1 at ends
            const float vAlpha = std::pow(edgeDist, 1.8f) * 0.85f;
            for (int px = 0; px < pw; ++px) {
                const float nx = static_cast<float>(px) / static_cast<float>(pw);
                const float edgeX = 1.0f - std::abs(nx - 0.5f) * 2.0f;
                const float hAlpha = std::pow(edgeX, 2.5f);
                const float alpha = vAlpha * hAlpha;
                if (alpha < 0.01f) continue;
                // Ice-blue core fading to white at brightest
                const int r = static_cast<int>(180.0f + alpha * 75.0f);
                const int g = static_cast<int>(210.0f + alpha * 45.0f);
                const int b = 255;
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(pillarGlowTex_.loadFromImage(img));
        pillarGlowTex_.setSmooth(true);
    }

    // --- Light mote texture (12x12) — soft diamond, cool white-blue ---
    {
        constexpr int ms = 12;
        constexpr float mhalf = 5.5f;
        sf::Image img({ms, ms}, sf::Color::Transparent);
        for (int py = 0; py < ms; ++py) {
            for (int px = 0; px < ms; ++px) {
                const float dx = std::abs(static_cast<float>(px) - mhalf);
                const float dy = std::abs(static_cast<float>(py) - mhalf);
                const float diamond = (dx + dy) / 7.5f;
                if (diamond >= 1.0f) continue;
                const float alpha = std::pow(1.0f - diamond, 1.8f);
                const int r = static_cast<int>(200.0f + alpha * 55.0f);
                const int g = static_cast<int>(220.0f + alpha * 35.0f);
                const int b = 255;
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 220.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(hpMoteTex_.loadFromImage(img));
        hpMoteTex_.setSmooth(true);
    }
}

void Game::generateSunTextures() {
    if (sunTexturesLoaded_) return;
    sunTexturesLoaded_ = true;

    // --- Sun disc texture (128x128) — radial gradient white-hot → golden → amber edge, with corona rays ---
    {
        constexpr int ds = 128;
        constexpr float dhalf = 63.5f;
        sf::Image img({ds, ds}, sf::Color::Transparent);
        for (int py = 0; py < ds; ++py) {
            for (int px = 0; px < ds; ++px) {
                const float dx = static_cast<float>(px) - dhalf;
                const float dy = static_cast<float>(py) - dhalf;
                const float dist = std::sqrt(dx * dx + dy * dy) / 64.0f; // 0=center, 1=edge
                if (dist >= 1.0f) continue;
                const float angle = std::atan2(dy, dx);

                // Corona rays: modulate brightness by angle
                constexpr int kRays = 16;
                const float rayAngle = std::cos(angle * static_cast<float>(kRays)) * 0.5f + 0.5f;
                const float rayStrength = 1.0f + rayAngle * 0.25f; // subtle bright streaks

                // Radial gradient: hot white core → golden → amber edge
                const float core = std::max(0.0f, 1.0f - dist * 6.0f);
                float alpha = std::pow(1.0f - dist, 1.8f) * rayStrength;
                alpha = std::min(1.0f, alpha);

                int r = 255;
                int g = static_cast<int>(220.0f + core * 35.0f - dist * 80.0f);
                int b = static_cast<int>(80.0f + core * 175.0f - dist * 30.0f);

                // Corona streak extra brightness
                if (rayAngle > 0.75f && dist > 0.55f && dist < 0.9f) {
                    g = std::min(255, g + 20);
                }

                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 250.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(std::clamp(g, 0, 255)),
                                       static_cast<std::uint8_t>(std::clamp(b, 0, 255)), a));
            }
        }
        static_cast<void>(sunDiscTex_.loadFromImage(img));
        sunDiscTex_.setSmooth(true);
    }

    // --- Sun ray texture (16x48) — elongated triangular wedge, golden-white gradient ---
    {
        constexpr int rw = 16;
        constexpr int rh = 48;
        constexpr float rhalf = 7.5f;
        sf::Image img({rw, rh}, sf::Color::Transparent);
        for (int py = 0; py < rh; ++py) {
            const float ny = static_cast<float>(py) / static_cast<float>(rh); // 0=base, 1=tip
            const float halfW = (1.0f - ny) * rhalf; // widest at base, pointed at tip
            for (int px = 0; px < rw; ++px) {
                const float dx = std::abs(static_cast<float>(px) - rhalf);
                if (dx > halfW) continue;
                const float edgeFade = 1.0f - dx / std::max(0.5f, halfW);
                const float alpha = edgeFade * (0.35f + ny * 0.65f); // brighter at tip
                const int r = 255;
                const int g = static_cast<int>(200.0f + ny * 55.0f);
                const int b = static_cast<int>(80.0f + ny * 120.0f);
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 200.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(sunRayTex_.loadFromImage(img));
        sunRayTex_.setSmooth(true);
    }

    // --- Sun mote texture (12x12) — warm golden diamond ---
    {
        constexpr int ms = 12;
        constexpr float mhalf = 5.5f;
        sf::Image img({ms, ms}, sf::Color::Transparent);
        for (int py = 0; py < ms; ++py) {
            for (int px = 0; px < ms; ++px) {
                const float dx = std::abs(static_cast<float>(px) - mhalf);
                const float dy = std::abs(static_cast<float>(py) - mhalf);
                const float diamond = (dx + dy) / 7.5f;
                if (diamond >= 1.0f) continue;
                const float alpha = std::pow(1.0f - diamond, 2.0f);
                const int r = 255;
                const int g = static_cast<int>(200.0f + alpha * 55.0f);
                const int b = static_cast<int>(80.0f + alpha * 100.0f);
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 210.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(sunMoteTex_.loadFromImage(img));
        sunMoteTex_.setSmooth(true);
    }
}

void Game::generateStarTextures() {
    if (starTexturesLoaded_) return;
    starTexturesLoaded_ = true;

    // --- Star sprite texture (32x32) — 4-pointed sharp diamond star, blue-white gradient ---
    {
        constexpr int ss = 32;
        constexpr float shalf = 15.5f;
        sf::Image img({ss, ss}, sf::Color::Transparent);
        for (int py = 0; py < ss; ++py) {
            for (int px = 0; px < ss; ++px) {
                const float dx = std::abs(static_cast<float>(px) - shalf);
                const float dy = std::abs(static_cast<float>(py) - shalf);
                // 4-point star: arms along axes AND diagonals
                const float axisDist = std::min(dx, dy);   // diagonal arms
                const float diagDist = std::abs(dx - dy) / 1.414f; // axis arms
                const float starDist = std::min(axisDist, diagDist);
                const float maxDist = 15.5f;
                const float normDist = starDist / maxDist;
                if (normDist >= 1.0f) continue;
                // Sharp falloff for crisp star shape
                const float alpha = std::pow(1.0f - normDist, 3.5f);
                // Blue-white core fading to blue at tips
                const float tipness = normDist;
                const int r = static_cast<int>(200.0f + tipness * 55.0f);
                const int g = static_cast<int>(210.0f + tipness * 45.0f);
                const int b = 255;
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 230.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(starSpriteTex_.loadFromImage(img));
        starSpriteTex_.setSmooth(true);
    }

    // --- Stardust texture (8x8) — soft white-blue diamond mote ---
    {
        constexpr int ms = 8;
        constexpr float mhalf = 3.5f;
        sf::Image img({ms, ms}, sf::Color::Transparent);
        for (int py = 0; py < ms; ++py) {
            for (int px = 0; px < ms; ++px) {
                const float dx = std::abs(static_cast<float>(px) - mhalf);
                const float dy = std::abs(static_cast<float>(py) - mhalf);
                const float diamond = (dx + dy) / 5.0f;
                if (diamond >= 1.0f) continue;
                const float alpha = std::pow(1.0f - diamond, 1.5f);
                const int r = 220;
                const int g = 230;
                const int b = 255;
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 200.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(starDustTex_.loadFromImage(img));
        starDustTex_.setSmooth(true);
    }
}

void Game::generateMoonTextures() {
    if (moonTexturesLoaded_) return;
    moonTexturesLoaded_ = true;

    // --- Crescent moon texture (64x64) — silver-blue crescent with glow ---
    {
        constexpr int ms = 64;
        constexpr float mcx = 31.5f;
        constexpr float mcy = 31.5f;
        constexpr float mr = 22.0f;
        sf::Image img({ms, ms}, sf::Color::Transparent);
        for (int py = 0; py < ms; ++py) {
            for (int px = 0; px < ms; ++px) {
                const float dx = static_cast<float>(px) - mcx;
                const float dy = static_cast<float>(py) - mcy;
                const float dist = std::sqrt(dx * dx + dy * dy);
                // Outer glow ring
                const float glowNorm = dist / (mr + 8.0f);
                if (glowNorm < 1.0f) {
                    const float glowAlpha = std::pow(1.0f - glowNorm, 2.5f) * 0.35f;
                    const auto ga = static_cast<std::uint8_t>(std::clamp(glowAlpha * 255.0f, 0.0f, 255.0f));
                    img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                                 sf::Color(180, 200, 240, ga));
                }
                // Crescent: main circle minus shadow circle offset upper-right
                if (dist > mr) continue;
                const float sdx = dx - 8.0f;
                const float sdy = dy + 6.0f;
                const float sdist = std::sqrt(sdx * sdx + sdy * sdy);
                if (sdist < mr * 0.85f) continue; // shadow cutout
                // Edge softness
                const float edge = 1.0f - std::abs(dist - mr) / 3.0f;
                const float coreAlpha = std::clamp(edge, 0.0f, 1.0f);
                // Silver-white crescent with blue tint at tips
                const float angle = std::atan2(dy, dx);
                const float tipness = std::abs(std::cos(angle)) * 0.5f + 0.5f;
                const int r = static_cast<int>(220.0f + tipness * 35.0f);
                const int g = static_cast<int>(225.0f + tipness * 30.0f);
                const int b = static_cast<int>(235.0f + tipness * 20.0f);
                const auto a = static_cast<std::uint8_t>(std::clamp(coreAlpha * 240.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(moonTex_.loadFromImage(img));
        moonTex_.setSmooth(true);
    }

    // --- Mist particle texture (16x16) — soft diffuse glow ---
    {
        constexpr int ps = 16;
        constexpr float pc = 7.5f;
        sf::Image img({ps, ps}, sf::Color::Transparent);
        for (int py = 0; py < ps; ++py) {
            for (int px = 0; px < ps; ++px) {
                const float dx = static_cast<float>(px) - pc;
                const float dy = static_cast<float>(py) - pc;
                const float dist = std::sqrt(dx * dx + dy * dy) / pc;
                if (dist >= 1.0f) continue;
                const float alpha = std::pow(1.0f - dist, 3.0f);
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 140.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(200, 215, 240, a));
            }
        }
        static_cast<void>(moonMistTex_.loadFromImage(img));
        moonMistTex_.setSmooth(true);
    }
}

void Game::generateHermitTextures() {
    if (hermitTexturesLoaded_) return;
    hermitTexturesLoaded_ = true;

    // --- Pond texture (64x64) — deep indigo center with radial ripple rings ---
    {
        constexpr int ps = 64;
        constexpr float pc = 31.5f;
        sf::Image img({ps, ps}, sf::Color::Transparent);
        for (int py = 0; py < ps; ++py) {
            for (int px = 0; px < ps; ++px) {
                const float dx = static_cast<float>(px) - pc;
                const float dy = static_cast<float>(py) - pc;
                const float dist = std::sqrt(dx * dx + dy * dy) / pc;
                if (dist >= 1.0f) continue;
                // Deep indigo center, fading to edge
                const float baseAlpha = std::pow(1.0f - dist, 2.0f);
                // Ripple rings
                const float ripple = std::abs(std::sin(dist * 14.0f)) * (1.0f - dist) * 0.3f;
                const float totalAlpha = baseAlpha * 0.7f + ripple;
                const int r = static_cast<int>(30.0f + dist * 20.0f);
                const int g = static_cast<int>(45.0f + dist * 30.0f);
                const int b = static_cast<int>(80.0f + dist * 40.0f);
                const auto a = static_cast<std::uint8_t>(std::clamp(totalAlpha * 200.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(hermitPondTex_.loadFromImage(img));
        hermitPondTex_.setSmooth(true);
    }

    // --- Lantern texture (12x12) — soft blue-white glow mote ---
    {
        constexpr int ls = 12;
        constexpr float lc = 5.5f;
        sf::Image img({ls, ls}, sf::Color::Transparent);
        for (int py = 0; py < ls; ++py) {
            for (int px = 0; px < ls; ++px) {
                const float dx = static_cast<float>(px) - lc;
                const float dy = static_cast<float>(py) - lc;
                const float dist = std::sqrt(dx * dx + dy * dy) / lc;
                if (dist >= 1.0f) continue;
                const float alpha = std::pow(1.0f - dist, 2.5f);
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 180.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(140, 170, 220, a));
            }
        }
        static_cast<void>(hermitLanternTex_.loadFromImage(img));
        hermitLanternTex_.setSmooth(true);
    }
}

void Game::generateHierophantTextures() {
    if (hierophantTexturesLoaded_) return;
    hierophantTexturesLoaded_ = true;

    // --- Light pillar texture (64x64) — white-gold vertical beam gradient ---
    {
        constexpr int ls = 64;
        sf::Image img({ls, ls}, sf::Color::Transparent);
        for (int py = 0; py < ls; ++py) {
            const float dy = std::abs(static_cast<float>(py) - 31.5f) / 20.0f;
            const float vertFalloff = std::exp(-dy * dy * 0.5f);
            for (int px = 0; px < ls; ++px) {
                const float dx = std::abs(static_cast<float>(px) - 31.5f) / 28.0f;
                const float horizFalloff = std::exp(-dx * dx * 1.5f);
                const float alpha = vertFalloff * horizFalloff;
                if (alpha < 0.01f) continue;
                const float tipness = std::abs(static_cast<float>(py) - 31.5f) / 31.5f;
                const int r = static_cast<int>(240.0f + tipness * 15.0f);
                const int g = static_cast<int>(200.0f - tipness * 40.0f);
                const int b = static_cast<int>(100.0f - tipness * 50.0f);
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 180.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(hierophantLightTex_.loadFromImage(img));
        hierophantLightTex_.setSmooth(true);
    }

    // --- Rune/Cross texture (32x32) — golden sacred cross ---
    {
        constexpr int rs = 32;
        constexpr float rc = 15.5f;
        sf::Image img({rs, rs}, sf::Color::Transparent);
        for (int py = 0; py < rs; ++py) {
            for (int px = 0; px < rs; ++px) {
                const float dx = std::abs(static_cast<float>(px) - rc);
                const float dy = std::abs(static_cast<float>(py) - rc);
                // Cross shape: thin vertical + horizontal arms
                const float vertArm = std::min(dx, 2.5f);
                const float horizArm = std::min(dy, 2.5f);
                const float crossDist = std::max(vertArm, horizArm) / 12.0f;
                if (crossDist >= 1.0f) continue;
                const float alpha = std::pow(1.0f - crossDist, 2.0f);
                const int r = 240;
                const int g = 210;
                const int b = 130;
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 220.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r),
                                       static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(hierophantRuneTex_.loadFromImage(img));
        hierophantRuneTex_.setSmooth(true);
    }
}

void Game::generateJusticeTextures() {
    if (justiceTexturesLoaded_) return;
    justiceTexturesLoaded_ = true;

    // --- Scales of justice texture (64x64) — golden balance with central pillar ---
    {
        constexpr int ss = 64;
        constexpr float sc = 31.5f;
        sf::Image img({ss, ss}, sf::Color::Transparent);
        for (int py = 0; py < ss; ++py) {
            for (int px = 0; px < ss; ++px) {
                const float dx = static_cast<float>(px) - sc;
                const float dy = static_cast<float>(py) - sc;
                // Central pillar (thin vertical bar)
                const float pillar = (std::abs(dx) < 2.5f && py > 10 && py < 52) ? 0.0f : 99.0f;
                // Horizontal beam
                const float beam = (std::abs(dy - 14.0f) < 2.5f && std::abs(dx) < 28.0f && px > 6 && px < 58) ? 0.0f : 99.0f;
                // Left pan chain
                const float lChainV = (std::abs(dx + 22.0f) < 1.5f && py > 22 && py < 38) ? 0.0f : 99.0f;
                // Right pan chain
                const float rChainV = (std::abs(dx - 22.0f) < 1.5f && py > 22 && py < 38) ? 0.0f : 99.0f;
                // Left pan (arc at bottom)
                const float lPanDx = dx + 22.0f;
                const float lPanDy = dy - 38.0f;
                const float lPan = (std::sqrt(lPanDx * lPanDx + lPanDy * lPanDy) < 10.0f && lPanDy > -8.0f) ? 0.0f : 99.0f;
                // Right pan
                const float rPanDx = dx - 22.0f;
                const float rPanDy = dy - 38.0f;
                const float rPan = (std::sqrt(rPanDx * rPanDx + rPanDy * rPanDy) < 10.0f && rPanDy > -8.0f) ? 0.0f : 99.0f;
                const float minDist = std::min({pillar, beam, lChainV, rChainV, lPan, rPan});
                if (minDist > 12.0f) continue;
                const float alpha = std::clamp(1.0f - minDist / 8.0f, 0.0f, 1.0f);
                const int r = 235;
                const int g = 200;
                const int b = 100;
                const auto a = static_cast<std::uint8_t>(alpha * 220.0f);
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(b), a));
            }
        }
        static_cast<void>(justiceScaleTex_.loadFromImage(img));
        justiceScaleTex_.setSmooth(true);
    }

    // --- Feather texture (16x16) — soft white feather shape ---
    {
        constexpr int fs = 16;
        constexpr float fc = 7.5f;
        sf::Image img({fs, fs}, sf::Color::Transparent);
        for (int py = 0; py < fs; ++py) {
            for (int px = 0; px < fs; ++px) {
                const float dx = static_cast<float>(px) - fc;
                const float dy = static_cast<float>(py) - fc;
                // Elliptical shape with narrower bottom
                const float vy = dy / 8.0f;
                const float vx = dx / (4.0f - 2.0f * std::abs(vy));
                const float dist = std::sqrt(vx * vx + vy * vy);
                if (dist >= 1.0f) continue;
                const float alpha = std::pow(1.0f - dist, 2.0f);
                const auto a = static_cast<std::uint8_t>(std::clamp(alpha * 200.0f, 0.0f, 255.0f));
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(245, 242, 235, a));
            }
        }
        static_cast<void>(justiceFeatherTex_.loadFromImage(img));
        justiceFeatherTex_.setSmooth(true);
    }
}

void Game::generateHangedManTextures() {
    if (hangedManTexturesLoaded_) return;
    hangedManTexturesLoaded_ = true;

    // --- Sacrifice mark texture (32x32) — crimson inverted cross emblem ---
    {
        constexpr int ms = 32;
        constexpr float mc = 15.5f;
        sf::Image img({ms, ms}, sf::Color::Transparent);
        for (int py = 0; py < ms; ++py) {
            for (int px = 0; px < ms; ++px) {
                const float dx = std::abs(static_cast<float>(px) - mc);
                const float dy = std::abs(static_cast<float>(py) - mc);
                // Inverted cross: long vertical bar + shorter horizontal top bar
                const float vertBar = (dx < 3.0f && py > 4 && py < 28) ? 0.0f : 99.0f;
                const float horizBar = (std::abs(dy - 8.0f) < 2.5f && dx < 12.0f && px > 4 && px < 28) ? 0.0f : 99.0f;
                // Circle halo
                const float circDist = std::sqrt((dx * dx + dy * dy)) - 12.0f;
                const float halo = std::abs(circDist) < 1.5f ? 0.0f : 99.0f;
                const float minDist = std::min({vertBar, horizBar, halo});
                if (minDist > 4.0f) continue;
                const float alpha = std::clamp(1.0f - minDist / 4.0f, 0.0f, 1.0f);
                const auto a = static_cast<std::uint8_t>(alpha * 220.0f);
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(200, 50, 50, a));
            }
        }
        static_cast<void>(hangedManMarkTex_.loadFromImage(img));
        hangedManMarkTex_.setSmooth(true);
    }
}

void Game::generateDevilTextures() {
    if (devilTexturesLoaded_) return;
    devilTexturesLoaded_ = true;

    // --- Pentagram / magic circle texture (64x64) ---
    {
        constexpr int ms = 64;
        constexpr float mc = 31.5f;
        sf::Image img({ms, ms}, sf::Color::Transparent);

        // Compute 5 outer points and 5 inner points for the pentagram star
        constexpr int numPts = 5;
        sf::Vector2f outer[numPts];
        sf::Vector2f inner[numPts];
        constexpr float outerR = 28.0f;
        constexpr float innerR = 11.0f;
        for (int i = 0; i < numPts; ++i) {
            const float angle = -3.14159265f / 2.0f + static_cast<float>(i) * 2.0f * 3.14159265f / static_cast<float>(numPts);
            outer[i] = {mc + outerR * std::cos(angle), mc + outerR * std::sin(angle)};
            const float innerAngle = angle + 3.14159265f / static_cast<float>(numPts);
            inner[i] = {mc + innerR * std::cos(innerAngle), mc + innerR * std::sin(innerAngle)};
        }

        for (int py = 0; py < ms; ++py) {
            for (int px = 0; px < ms; ++px) {
                const float fx = static_cast<float>(px);
                const float fy = static_cast<float>(py);
                const float dx = fx - mc;
                const float dy = fy - mc;
                const float dist = std::sqrt(dx * dx + dy * dy);

                float minDist = 99.0f;

                // Outer circle ring
                const float circRing = std::abs(dist - outerR);
                if (circRing < 1.8f) minDist = std::min(minDist, circRing);

                // Inner circle ring
                const float innerCircRing = std::abs(dist - innerR * 0.8f);
                if (innerCircRing < 1.2f) minDist = std::min(minDist, innerCircRing);

                // Pentagram star lines: connect outer[i] to inner[i], and inner[i] to outer[(i+1)%5]
                auto segDist = [](sf::Vector2f a, sf::Vector2f b, sf::Vector2f p) {
                    const sf::Vector2f ab = b - a;
                    const sf::Vector2f ap = p - a;
                    const float len2 = ab.x * ab.x + ab.y * ab.y;
                    if (len2 < 0.0001f) return std::sqrt(ap.x * ap.x + ap.y * ap.y);
                    const float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f);
                    const sf::Vector2f closest = a + ab * t;
                    const sf::Vector2f diff = p - closest;
                    return std::sqrt(diff.x * diff.x + diff.y * diff.y);
                };
                for (int i = 0; i < numPts; ++i) {
                    const int j = (i + 1) % numPts;
                    const auto d1 = segDist(outer[i], inner[i], {fx, fy});
                    if (d1 < 1.5f) minDist = std::min(minDist, d1);
                    const auto d2 = segDist(inner[i], outer[j], {fx, fy});
                    if (d2 < 1.5f) minDist = std::min(minDist, d2);
                }

                // Center dot
                if (dist < 2.5f) minDist = std::min(minDist, dist);

                if (minDist > 4.0f) continue;
                const float alpha = std::clamp(1.0f - minDist / 4.0f, 0.0f, 1.0f);
                const auto a = static_cast<std::uint8_t>(alpha * 220.0f);
                // Dark purple-red glow
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(180, 30, 60, a));
            }
        }
        static_cast<void>(devilPentagramTex_.loadFromImage(img));
        devilPentagramTex_.setSmooth(true);
    }
}

void Game::generateChariotTextures() {
    if (chariotTexturesLoaded_) return;
    chariotTexturesLoaded_ = true;

    // --- Gear/wheel texture (48x48) — golden 8-tooth charge gear ---
    {
        constexpr int ms = 48;
        constexpr float mc = 23.5f;
        sf::Image img({ms, ms}, sf::Color::Transparent);

        for (int py = 0; py < ms; ++py) {
            for (int px = 0; px < ms; ++px) {
                const float fx = static_cast<float>(px);
                const float fy = static_cast<float>(py);
                const float dx = fx - mc;
                const float dy = fy - mc;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float angle = std::atan2(dy, dx);

                float minDist = 99.0f;

                // Outer ring
                const float outerR = 21.0f;
                const float ringDist = std::abs(dist - outerR);
                if (ringDist < 1.5f) minDist = std::min(minDist, ringDist);

                // Inner ring
                const float innerR = 7.0f;
                const float innerRingDist = std::abs(dist - innerR);
                if (innerRingDist < 1.2f) minDist = std::min(minDist, innerRingDist);

                // 8 gear teeth — radial spikes
                constexpr int teeth = 8;
                for (int t = 0; t < teeth; ++t) {
                    const float toothAngle = static_cast<float>(t) * 2.0f * 3.14159265f / static_cast<float>(teeth);
                    const float da = angle - toothAngle;
                    // Normalize angle diff to [-PI, PI]
                    const float nda = std::atan2(std::sin(da), std::cos(da));
                    // Tooth is a radial bar from innerR to outerR+4 with angular width
                    if (dist > innerR - 1.0f && dist < outerR + 4.0f && std::abs(nda) < 0.22f) {
                        const float td = std::min(std::abs(nda) / 0.22f, (dist < innerR ? innerR - dist : (dist > outerR + 3.0f ? dist - outerR - 3.0f : 0.0f)));
                        if (td < 0.8f) minDist = std::min(minDist, td * 2.0f);
                    }
                }

                // Center hub
                if (dist < 4.0f) minDist = std::min(minDist, dist);

                // Spokes from center to outer ring
                for (int s = 0; s < 4; ++s) {
                    const float spokeAngle = static_cast<float>(s) * 3.14159265f / 2.0f;
                    const float da = angle - spokeAngle;
                    const float nda = std::atan2(std::sin(da), std::cos(da));
                    if (dist < outerR && std::abs(nda) < 0.08f) {
                        minDist = std::min(minDist, std::abs(nda) * 15.0f);
                    }
                }

                if (minDist > 4.0f) continue;
                const float alpha = std::clamp(1.0f - minDist / 4.0f, 0.0f, 1.0f);
                const auto a = static_cast<std::uint8_t>(alpha * 220.0f);
                img.setPixel({static_cast<unsigned>(px), static_cast<unsigned>(py)},
                             sf::Color(240, 180, 40, a));
            }
        }
        static_cast<void>(chariotGearTex_.loadFromImage(img));
        chariotGearTex_.setSmooth(true);
    }
}

// ======== Card Event System ========

void Game::loadCardTextures() {
    for (int i = 0; i < kNumCardTextures; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%02d.png", i);
        for (const auto* prefix : {"assets/", "../assets/"}) {
            const std::string path = std::string(prefix) + buf;
            if (std::filesystem::exists(path)) {
                static_cast<void>(cardTextures_[i].loadFromFile(path));
                cardTextures_[i].setSmooth(false);
                break;
            }
        }
    }
    for (const auto* prefix : {"assets/", "../assets/"}) {
        const std::string backPath = std::string(prefix) + "back.png";
        if (std::filesystem::exists(backPath)) {
            static_cast<void>(cardBackTex_.loadFromFile(backPath));
            cardBackTex_.setSmooth(false);
            break;
        }
    }
}

void Game::triggerCardEvent() {
    if (cardEventState_ != CardEventState::Idle) return;
    if (boardMode_ != Board::Mode::Obstacle) return;
    if (gameOver_) return;

    constexpr std::array<CardType, 22> kAllCards = {
        CardType::Fool, CardType::Magician, CardType::HighPriestess, CardType::Empress,
        CardType::Emperor, CardType::Hierophant, CardType::Lovers, CardType::Chariot,
        CardType::Strength, CardType::Hermit, CardType::WheelOfFortune, CardType::Justice,
        CardType::HangedMan, CardType::Death, CardType::Temperance, CardType::Devil,
        CardType::Tower, CardType::Star, CardType::Moon, CardType::Sun,
        CardType::Judgement, CardType::World
    };

    // Build pool: Hard gets all 22; Easy/Medium exclude fate-reliant cards
    std::vector<CardType> pool;
    pool.reserve(22);
    for (auto c : kAllCards) {
        if (aiDifficulty_ != AIDifficulty::Hard &&
            (c == CardType::WheelOfFortune || c == CardType::Tower))
            continue;
        pool.push_back(c);
    }

    static thread_local std::mt19937 rng(std::random_device{}());
    std::shuffle(pool.begin(), pool.end(), rng);

    drawnCards_[0] = pool[0];
    drawnCards_[1] = pool[1];
    drawnCards_[2] = pool[2];
    selectedCardIndex_ = -1;
    chosenCardIdx_ = -1;
    cardEventState_ = CardEventState::Omen;
    cardEventClock_.restart();
    shakeIntensity_ = 0.0f;
    darkenAlpha_ = 0.0f;
    messengerAlpha_ = 0.0f;
    messengerPos_ = {640.0f, 410.0f};
    cardDealProgress_ = 0.0f;
    cardFlipProgress_ = 0.0f;
    cardFloatTime_ = 0.0f;
    cardRevealProgress_ = 0.0f;
    cardEventSpeech_.clear();
    messengerFloatClock_.restart();

    // Load messenger texture on first use
    if (messengerTex_.getSize().x == 0) {
        for (const auto* prefix : {"assets/", "../assets/"}) {
            const std::string path = std::string(prefix) + "Agis.png";
            if (std::filesystem::exists(path)) {
                static_cast<void>(messengerTex_.loadFromFile(path));
                messengerTex_.setSmooth(false);
                break;
            }
        }
    }

    // Load crown texture on first use (Emperor card)
    if (crownTex_.getSize().x == 0) {
        for (const auto* prefix : {"assets/", "../assets/"}) {
            const std::string path = std::string(prefix) + "crown.png";
            if (std::filesystem::exists(path)) {
                static_cast<void>(crownTex_.loadFromFile(path));
                crownTex_.setSmooth(false);
                break;
            }
        }
    }

    // Clear opponent speech during card event
    speechText_.clear();

    // Network: server sends card event data to client
    if (server_) {
        server_->sendCardEvent(static_cast<int>(drawnCards_[0]),
                               static_cast<int>(drawnCards_[1]),
                               static_cast<int>(drawnCards_[2]));
    }
}

void Game::handleCardSelectionClick(sf::Vector2f mousePos) {
    if (cardEventState_ != CardEventState::Choosing) return;
    // In network mode, only the wheel-chosen player can select
    if (networkMode_ && !iAmPicker_) return;

    constexpr float cardScale = 2.8f;
    constexpr float cardW = 73.0f * cardScale;
    constexpr float cardH = 113.0f * cardScale;
    constexpr float spacing = 100.0f;
    constexpr float totalW = cardW * 3.0f + spacing * 2.0f;
    constexpr float startX = (1280.0f - totalW) * 0.5f;
    constexpr float startY = 320.0f;
    constexpr float boxH = 98.0f;
    constexpr float boxGap = 10.0f;

    for (int i = 0; i < kCardsPerEvent; ++i) {
        const float cx = startX + static_cast<float>(i) * (cardW + spacing);
        sf::FloatRect cardBounds({cx, startY}, {cardW, cardH + boxGap + boxH});
        if (cardBounds.contains(mousePos)) {
            playButtonSound();
            chosenCardIdx_ = i;
            selectedCardIndex_ = i;
            cardEventState_ = CardEventState::Reveal;
            cardEventClock_.restart();
            cardRevealProgress_ = 0.0f;
            cardEventSpeech_.clear();
            // Send selection to opponent in network mode
            if (networkMode_) {
                if (server_) {
                    server_->sendCardSelected(i);
                    // Server generates RNG seed for deterministic card effects
                    cardEffectSeed_ = static_cast<std::uint32_t>(std::rand());
                    cardEffectSeedSet_ = true;
                    server_->sendCardEffectSeed(cardEffectSeed_);
                } else if (client_) {
                    client_->sendCardSelected(i);
                }
            }
            return;
        }
    }
}

void Game::applyCardEffect(CardType card) {
    // Determine player/opponent piece based on who drew the card
    Board::Piece playerPiece;
    Board::Piece opponentPiece;
    if (networkMode_) {
        // The drawer's piece color is "player"; the other is "opponent"
        const bool drawerIsHost = iAmPicker_ ? isNetworkHost_ : !isNetworkHost_;
        playerPiece = drawerIsHost ? Board::Piece::Black : Board::Piece::White;
    } else {
        playerPiece = Board::Piece::Black;
    }
    opponentPiece = (playerPiece == Board::Piece::Black) ? Board::Piece::White : Board::Piece::Black;

    // In network mode, use the server-provided seed for deterministic RNG
    std::mt19937 rng(networkMode_ && cardEffectSeedSet_
        ? static_cast<std::mt19937::result_type>(cardEffectSeed_)
        : std::random_device{}());
    cardEffectSeedSet_ = false;

    switch (card) {

    // --- Obstacle Operations ---
    case CardType::Emperor: {
        // Remove 2 random obstacles with golden dissolution animation
        auto obs = board_.obstaclePositions();
        int count = std::min(2, static_cast<int>(obs.size()));
        if (count > 0) {
            std::shuffle(obs.begin(), obs.end(), rng);
            for (int i = 0; i < count; ++i) {
                ObstacleRemovalAnim anim;
                anim.gridPos = obs[i];
                anim.pixelPos = board_.cellToPixel(obs[i].x, obs[i].y);
                anim.emperorStyle = true;
                obstacleRemovalAnims_.push_back(anim);
            }
        }
        break;
    }

    case CardType::WheelOfFortune: {
        // Deferred: wheel spins, orbs scatter, obstacles redistribute
        const int count = board_.obstacleCount();
        wheelFortuneObstacleCount_ = count;
        wheelFortuneSavedObstacles_.clear();
        for (const auto& op : board_.obstaclePositions()) {
            wheelFortuneSavedObstacles_.push_back({static_cast<float>(op.x), static_cast<float>(op.y)});
        }
        wheelFortuneAnimPending_ = true;
        wheelFortuneAnimClock_.restart();
        wheelFortuneAngle_ = 0.0f;
        wheelFortuneAlpha_ = 0.0f;
        wheelFortuneRadius_ = 0.0f;
        wheelFortuneOrbProgress_ = 0.0f;
        wheelFortuneObstaclesCleared_ = false;
        wheelFortuneObstaclesRegenerated_ = false;
        wheelFortuneParticles_.clear();
        break;
    }

    case CardType::Tower: {
        // Deferred: lightning strikes, obstacles crumble, new ones rise
        const int count = std::min(8, board_.obstacleCount());
        towerObstacleCount_ = count;
        // Save obstacle positions for rubble animation
        towerSavedObstacles_.clear();
        for (const auto& op : board_.obstaclePositions()) {
            towerSavedObstacles_.push_back({static_cast<float>(op.x), static_cast<float>(op.y)});
        }
        towerAnimPending_ = true;
        towerAnimClock_.restart();
        towerFlashAlpha_ = 0.0f;
        towerLightningAlpha_ = 0.0f;
        towerRubbleAlpha_ = 0.0f;
        towerRebirthAlpha_ = 0.0f;
        towerObstaclesCleared_ = false;
        towerObstaclesRegenerated_ = false;
        towerParticles_.clear();
        break;
    }

    // --- Piece Operations ---
    case CardType::Empress: {
        // Deferred animation: vine grows from nearest player piece to best adjacent cell
        auto adjacent = board_.findAdjacentEmptyCells(playerPiece);
        if (!adjacent.empty()) {
            std::sort(adjacent.begin(), adjacent.end(), [](const auto& a, const auto& b) {
                const int da = std::abs(a.x - 7) + std::abs(a.y - 7);
                const int db = std::abs(b.x - 7) + std::abs(b.y - 7);
                return da < db;
            });
            empressTargetCell_ = adjacent[0];

            // Find nearest player piece to the target as vine source
            int bestDist = 999;
            empressSourcePiece_ = {-1, -1};
            for (int r = 0; r < Board::kBoardSize; ++r) {
                for (int c = 0; c < Board::kBoardSize; ++c) {
                    if (board_.pieceAt(r, c) == playerPiece) {
                        const int d = std::abs(r - empressTargetCell_.x) + std::abs(c - empressTargetCell_.y);
                        if (d < bestDist) { bestDist = d; empressSourcePiece_ = {r, c}; }
                    }
                }
            }

            if (empressSourcePiece_.x >= 0) {
                empressPlayerPiece_ = playerPiece;
                if (!empressTexturesLoaded_) generateEmpressTextures();
                empressAnimPending_ = true;
                empressPiecePlaced_ = false;
                empressAnimClock_.restart();
                empressVineProgress_ = 0.0f;
                empressBloomPhase_ = 0.0f;
                empressPetals_.clear();
                empressFireflies_.clear();
            }
        }
        break;
    }

    case CardType::Lovers: {
        // Start animation instead of swapping instantly
        // Collect all black and white piece positions
        std::vector<sf::Vector2i> blacks, whites;
        for (int r = 0; r < Board::kBoardSize; ++r) {
            for (int c = 0; c < Board::kBoardSize; ++c) {
                const auto p = board_.pieceAt(r, c);
                if (p == Board::Piece::Black) blacks.push_back({r, c});
                else if (p == Board::Piece::White) whites.push_back({r, c});
            }
        }
        if (!blacks.empty() && !whites.empty()) {
            // Ensure heart textures are loaded
            if (!heartTexturesLoaded_) generateHeartTextures();

            const auto& bp = blacks[std::uniform_int_distribution<std::size_t>(0, blacks.size() - 1)(rng)];
            const auto& wp = whites[std::uniform_int_distribution<std::size_t>(0, whites.size() - 1)(rng)];

            // Store piece info and clear from board for flying animation
            loversPieceA_ = bp;
            loversPieceB_ = wp;
            loversPieceAType_ = board_.pieceAt(bp.x, bp.y);
            board_.removePieceAt(bp.x, bp.y);
            board_.removePieceAt(wp.x, wp.y);

            // Precompute bezier arc paths
            const auto pxA = board_.cellToPixel(loversPieceA_.x, loversPieceA_.y);
            const auto pxB = board_.cellToPixel(loversPieceB_.x, loversPieceB_.y);
            loversMidpoint_ = {(pxA.x + pxB.x) * 0.5f, (pxA.y + pxB.y) * 0.5f};

            // Control point: midpoint displaced perpendicular to AB, upward bias
            const float dx = pxB.x - pxA.x;
            const float dy = pxB.y - pxA.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            // Perpendicular direction, pick the one pointing upward (smaller Y)
            float perpX = -dy;
            float perpY = dx;
            if (perpY > 0) { perpX = -perpX; perpY = -perpY; }
            const float perpLen = std::sqrt(perpX * perpX + perpY * perpY);
            const float offsetDist = std::max(40.0f, dist * 0.5f);
            const float cpX = loversMidpoint_.x + (perpX / perpLen) * offsetDist;
            const float cpY = loversMidpoint_.y + (perpY / perpLen) * offsetDist;

            // Sample 36 points along quadratic bezier for each direction
            constexpr int kArcSamples = 36;
            loversArcPathA_.resize(kArcSamples);
            loversArcPathB_.resize(kArcSamples);
            for (int i = 0; i < kArcSamples; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(kArcSamples - 1);
                const float u = 1.0f - t;
                // Piece A → B
                loversArcPathA_[i] = {u*u*pxA.x + 2*u*t*cpX + t*t*pxB.x,
                                      u*u*pxA.y + 2*u*t*cpY + t*t*pxB.y};
                // Piece B → A (reverse direction along same arc)
                loversArcPathB_[i] = {u*u*pxB.x + 2*u*t*cpX + t*t*pxA.x,
                                      u*u*pxB.y + 2*u*t*cpY + t*t*pxA.y};
            }

            // Start animation
            loversAnimPending_ = true;
            loversAnimClock_.restart();
            loversArcProgress_ = 0.0f;
            loversHeartParticles_.clear();
            loversFloatingTextAlpha_ = 0.0f;
        }
        break;
    }

    case CardType::Strength: {
        strengthProtectionRemaining_ = 3;
        strengthProtectedPos_ = {-1, -1};
        board_.setStrengthPos(std::nullopt);
        break;
    }

    // --- Piece Removal ---
    case CardType::HangedMan: {
        // Select pieces for sacrifice — removal deferred to animation
        hangedManSacrificePos_ = {-1, -1};
        hangedManTargetA_ = {-1, -1};
        hangedManTargetB_ = {-1, -1};
        // Select 1 own piece to sacrifice
        std::vector<sf::Vector2i> ownPieces;
        for (int r = 0; r < Board::kBoardSize; ++r)
            for (int c = 0; c < Board::kBoardSize; ++c)
                if (board_.pieceAt(r, c) == playerPiece) ownPieces.push_back({r, c});
        if (!ownPieces.empty())
            hangedManSacrificePos_ = ownPieces[std::uniform_int_distribution<std::size_t>(0, ownPieces.size() - 1)(rng)];
        // Select up to 2 from opponent's longest chain
        auto chain = board_.findLongestChainCells(opponentPiece);
        if (chain.size() >= 2) {
            std::shuffle(chain.begin(), chain.end(), rng);
            hangedManTargetA_ = chain[0];
            hangedManTargetB_ = chain[1];
        } else if (chain.size() == 1) {
            hangedManTargetA_ = chain[0];
        }
        winner_ = Board::Piece::None;
        gameOver_ = false;
        if (!hangedManTexturesLoaded_) generateHangedManTextures();
        hangedManAnimPending_ = true;
        hangedManAnimClock_.restart();
        hangedManOverlayAlpha_ = 0.0f;
        hangedManSacrificeGlowAlpha_ = 0.0f;
        hangedManThreadProgress_ = 0.0f;
        hangedManTargetGlowAlpha_ = 0.0f;
        hangedManMarkAlpha_ = 0.0f;
        hangedManSacrificed_ = false;
        hangedManTargetsRemoved_ = false;
        hangedManParticles_.clear();
        break;
    }

    case CardType::Death: {
        // Defer: death spread animation plays, then area clears
        if (const auto lastMove = board_.lastMovePosition(); lastMove.has_value()) {
            deathAnimPending_ = true;
            deathCenter_ = *lastMove;
            deathAnimClock_.restart();
            deathSpreadStep_ = 0;
            deathFlashAlpha_ = 0.0f;
        }
        break;
    }

    case CardType::Devil: {
        // Deferred: pentagram descends, dark flames consume target piece
        auto chain = board_.findLongestChainCells(opponentPiece);
        if (!chain.empty()) {
            const auto& p = chain[chain.size() / 2];
            devilTargetPos_ = p;
            if (!devilTexturesLoaded_) generateDevilTextures();
            devilAnimPending_ = true;
            devilAnimClock_.restart();
            devilOverlayAlpha_ = 0.0f;
            devilPentagramAngle_ = 0.0f;
            devilPentagramAlpha_ = 0.0f;
            devilPentagramScale_ = 0.5f;
            devilFlameIntensity_ = 0.0f;
            devilScorchAlpha_ = 0.0f;
            devilPieceRemoved_ = false;
            devilParticles_.clear();
        }
        winner_ = Board::Piece::None;
        gameOver_ = false;
        break;
    }

    // --- Turn / Rule / Special ---
    case CardType::Fool: {
        foolActive_ = true;
        foolAnimPending_ = true;
        foolAnimClock_.restart();
        foolStarPos_ = {640.0f, 120.0f}; // start above board
        foolStarBounceT_ = 0.0f;
        foolOverlayAlpha_ = 0.0f;
        foolStarAlpha_ = 0.0f;
        foolStarAngle_ = 0.0f;
        foolStarScale_ = 1.0f;
        foolBurstAlpha_ = 0.0f;
        foolParticles_.clear();
        break;
    }

    case CardType::Magician: {
        // Deferred: magic arc from source to symmetric target, piece materializes
        if (const auto lastMove = board_.lastMovePosition(); lastMove.has_value()) {
            const int symRow = 14 - lastMove->x;
            const int symCol = 14 - lastMove->y;
            if (symRow >= 0 && symRow < Board::kBoardSize &&
                symCol >= 0 && symCol < Board::kBoardSize &&
                board_.canPlaceAt(symRow, symCol)) {
                magicianSourcePos_ = *lastMove;
                magicianTargetPos_ = {symRow, symCol};
                magicianAnimPending_ = true;
                magicianAnimClock_.restart();
                magicianOverlayAlpha_ = 0.0f;
                magicianRippleAlpha_ = 0.0f;
                magicianArcProgress_ = 0.0f;
                magicianPortalAlpha_ = 0.0f;
                magicianSpawnAlpha_ = 0.0f;
                magicianPiecePlaced_ = false;
                magicianParticles_.clear();
            }
        }
        break;
    }

    case CardType::HighPriestess: {
        // Deferred animation: pillars descend, cross beam purifies row+column
        if (const auto lastMove = board_.lastMovePosition(); lastMove.has_value()) {
            hpCrossCenter_ = *lastMove;

            // Collect dissolving pieces on the cross for visual animation
            hpDissolvingPieces_.clear();
            for (int i = 0; i < Board::kBoardSize; ++i) {
                // Row
                const auto rp = board_.pieceAt(hpCrossCenter_.x, i);
                if (rp != Board::Piece::None) {
                    hpDissolvingPieces_.push_back({board_.cellToPixel(hpCrossCenter_.x, i), rp, 0.0f});
                }
                // Column (skip center to avoid duplicate)
                if (i != hpCrossCenter_.x) {
                    const auto cp = board_.pieceAt(i, hpCrossCenter_.y);
                    if (cp != Board::Piece::None) {
                        hpDissolvingPieces_.push_back({board_.cellToPixel(i, hpCrossCenter_.y), cp, 0.0f});
                    }
                }
            }

            // Queue obstacle dissolutions
            for (const auto& op : board_.obstaclePositions()) {
                if (op.x == hpCrossCenter_.x || op.y == hpCrossCenter_.y) {
                    ObstacleRemovalAnim anim;
                    anim.gridPos = op;
                    anim.pixelPos = board_.cellToPixel(op.x, op.y);
                    obstacleRemovalAnims_.push_back(anim);
                }
            }

            if (!hpTexturesLoaded_) generateHighPriestessTextures();
            highPriestessAnimPending_ = true;
            hpPiecesRemoved_ = false;
            hpAnimClock_.restart();
            hpPillarAlpha_ = 0.0f;
            hpPillarYOffset_ = -120.0f;
            hpBeamProgress_ = 0.0f;
            hpCrescentAlpha_ = 0.0f;
            hpMotes_.clear();
        }
        break;
    }

    case CardType::Chariot: {
        chariotPending_ = true;
        break;
    }

    case CardType::Hermit: {
        hermitRemaining_ = 2;
        if (!hermitTexturesLoaded_) generateHermitTextures();
        hermitAnimPending_ = true;
        hermitAnimClock_.restart();
        hermitOverlayAlpha_ = 0.0f;
        hermitPondRadius_ = 0.0f;
        hermitPondAlpha_ = 0.0f;
        hermitRingAlpha_ = 0.0f;
        hermitLanterns_.clear();
        hermitRipples_.clear();
        break;
    }

    case CardType::Justice: {
        // Select 1 random piece from each side for removal — deferred to animation
        std::vector<sf::Vector2i> blacks, whites;
        for (int r = 0; r < Board::kBoardSize; ++r) {
            for (int c = 0; c < Board::kBoardSize; ++c) {
                const auto p = board_.pieceAt(r, c);
                if (p == Board::Piece::Black) blacks.push_back({r, c});
                else if (p == Board::Piece::White) whites.push_back({r, c});
            }
        }
        justiceBlackPiece_ = {-1, -1};
        justiceWhitePiece_ = {-1, -1};
        if (!blacks.empty())
            justiceBlackPiece_ = blacks[std::uniform_int_distribution<std::size_t>(0, blacks.size() - 1)(rng)];
        if (!whites.empty())
            justiceWhitePiece_ = whites[std::uniform_int_distribution<std::size_t>(0, whites.size() - 1)(rng)];
        winner_ = Board::Piece::None;
        gameOver_ = false;
        if (!justiceTexturesLoaded_) generateJusticeTextures();
        justiceAnimPending_ = true;
        justiceAnimClock_.restart();
        justiceScaleAlpha_ = 0.0f;
        justiceLightAlpha_ = 0.0f;
        justiceScreenBrightAlpha_ = 0.0f;
        justicePieceGlowAlpha_ = 0.0f;
        justicePiecesRemoved_ = false;
        justiceFeathers_.clear();
        break;
    }

    case CardType::Sun: {
        // Clear obstacles in 5x5 area centered at (7,7) with generic dissolve
        for (const auto& op : board_.obstaclePositions()) {
            if (op.x >= 5 && op.x <= 9 && op.y >= 5 && op.y <= 9) {
                ObstacleRemovalAnim anim;
                anim.gridPos = op;
                anim.pixelPos = board_.cellToPixel(op.x, op.y);
                obstacleRemovalAnims_.push_back(anim);
            }
        }
        // Trigger Sun radiant animation
        if (!sunTexturesLoaded_) generateSunTextures();
        sunAnimPending_ = true;
        sunAnimClock_.restart();
        sunDiscScale_ = 0.0f;
        sunDiscAlpha_ = 0.0f;
        sunDiscYOffset_ = -160.0f;
        sunRotation_ = 0.0f;
        sunRayAlpha_ = 0.0f;
        sunZoneGlowAlpha_ = 0.0f;
        sunShockwaveRadius_ = 0.0f;
        sunShockwaveAlpha_ = 0.0f;
        sunMotes_.clear();
        break;
    }

    case CardType::World: {
        // Trigger golden mandala sealing animation instead of instant removal
        if (!worldTexturesLoaded_) generateWorldTextures();
        worldAnimPending_ = true;
        worldAnimClock_.restart();
        mandalaRotation_ = 0.0f;
        mandalaScale_ = 0.0f;
        mandalaAlpha_ = 0.0f;
        mandalaCenter_ = board_.cellToPixel(Board::kBoardSize / 2, Board::kBoardSize / 2);
        // Compute seal target to center of undo button
        // Non-network: button at {790, 674} size {402, 62} → center {991, 705}
        // Network:     button at {790, 674} size {196, 62} → center {888, 705}
        sealTarget_ = networkMode_
            ? sf::Vector2f{790.0f + 196.0f * 0.5f, 674.0f + 62.0f * 0.5f}
            : sf::Vector2f{790.0f + 402.0f * 0.5f, 674.0f + 62.0f * 0.5f};
        ringRadii_ = {};
        ringAlphas_ = {};
        sealFlashAlpha_ = 0.0f;
        amberVignetteAlpha_ = 0.0f;
        worldSealTriggered_ = false;
        worldParticles_.clear();
        break;
    }

    case CardType::Hierophant: {
        hierophantRemaining_ = 2;
        if (!hierophantTexturesLoaded_) generateHierophantTextures();
        hierophantAnimPending_ = true;
        hierophantAnimClock_.restart();
        hierophantLightAlpha_ = 0.0f;
        hierophantBoundaryAlpha_ = 0.0f;
        hierophantSealAlpha_ = 0.0f;
        hierophantOuterDimAlpha_ = 0.0f;
        hierophantCornerAlpha_ = 0.0f;
        hierophantParticles_.clear();
        break;
    }

    case CardType::Temperance: {
        temperanceRemaining_ = 2;
        board_.setTemperanceActive(true);
        temperanceAnimPending_ = true;
        temperanceAnimClock_.restart();
        temperanceOverlayAlpha_ = 0.0f;
        temperanceWingAlpha_ = 0.0f;
        temperanceHaloRadius_ = 0.0f;
        temperanceHaloAlpha_ = 0.0f;
        temperanceMarkAlpha_ = 0.0f;
        temperanceMarkAngle_ = 0.0f;
        temperanceParticles_.clear();
        break;
    }

    case CardType::Star: {
        // Private effect: only the card drawer sees the guiding star
        if (!networkMode_ || iAmPicker_) {
            auto bestMove = findBestAiMove();
            if (bestMove.has_value()) {
                starHighlightPos_ = *bestMove;
                starHighlightValid_ = true;
                if (!starTexturesLoaded_) generateStarTextures();
                starAnimPending_ = true;
                starInPersistentMode_ = false;
                starAnimClock_.restart();
                starYOffset_ = -60.0f;
                starScale_ = 0.0f;
                starAlpha_ = 0.0f;
                starRotation_ = 0.0f;
                starRayAlpha_ = 0.0f;
                starDustParticles_.clear();
            }
        }
        break;
    }

    case CardType::Moon: {
        moonActive_ = true;
        cardDeferredTurn_ = false;  // Keep turn with player; AI's next move is offset
        if (!moonTexturesLoaded_) generateMoonTextures();
        moonAnimPending_ = true;
        moonAnimClock_.restart();
        moonAlpha_ = 0.0f;
        moonYOffset_ = -40.0f;
        moonMistAlpha_ = 0.0f;
        moonOverlayAlpha_ = 0.0f;
        moonMistParticles_.clear();
        break;
    }

    case CardType::Judgement: {
        if (board_.totalPieceCount() >= 20) {
            judgementSavedObstacles_.clear();
            for (const auto& op : board_.obstaclePositions()) {
                judgementSavedObstacles_.push_back({static_cast<float>(op.x), static_cast<float>(op.y)});
            }
            judgementAnimPending_ = true;
            judgementAnimClock_.restart();
            judgementLightBeamProgress_ = 0.0f;
            judgementDarkenAlpha_ = 0.0f;
            judgementObstaclesCleared_ = false;
            judgementParticles_.clear();
            judgementRings_.clear();
        }
        break;
    }

    }
}

std::string Game::playerModeName() const {
    return aiEnabled_ ? "人机对战" : "双人对战";
}

std::string Game::difficultyName() const {
    switch (aiDifficulty_) {
    case AIDifficulty::Easy:
        return "简单";
    case AIDifficulty::Hard:
        return "困难";
    case AIDifficulty::Medium:
    default:
        return "中等";
    }
}
