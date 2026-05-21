#pragma once

#include "Board.hpp"
#include "NetworkClient.hpp"
#include "NetworkServer.hpp"

#include <SFML/Audio/Music.hpp>
#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>
#include <SFML/Graphics.hpp>
#include <SFML/System/Clock.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

class Game {
public:
    Game();
    void run();

private:
    enum class Scene {
        MainMenu,
        Rules,
        ModeSelect,
        DifficultySelect,
        NetworkLobby,
        RoomSetup,
        NetworkWait,
        Playing
    };

    enum class AIDifficulty {
        Easy,
        Medium,
        Hard
    };

    enum class CardType {
        Fool = 0,
        Magician = 1,
        HighPriestess = 2,
        Empress = 3,
        Emperor = 4,
        Hierophant = 5,
        Lovers = 6,
        Chariot = 7,
        Strength = 8,
        Hermit = 9,
        WheelOfFortune = 10,
        Justice = 11,
        HangedMan = 12,
        Death = 13,
        Temperance = 14,
        Devil = 15,
        Tower = 16,
        Star = 17,
        Moon = 18,
        Sun = 19,
        Judgement = 20,
        World = 21
    };

    enum class CardEventState { Idle, Omen, MessengerBig, MessengerWait, WheelSpin, DealCards, FlipCards, Choosing, Reveal, Applied };

    enum class TransitionState { None, FadingOut, FadingIn };

    struct UiButton {
        sf::FloatRect bounds;
        std::string label;
        sf::Color fill;
        sf::Color accent;
    };

    void processEvents();
    void update();
    void render();

    void startMatch(Board::Mode mode, bool aiEnabled, AIDifficulty difficulty = AIDifficulty::Medium);
    void returnToMainMenu();
    void startTransition(std::function<void()> action);
    void restart();
    void undoMove();
    void undoNetworkMove();
    void surrender();
    void tryPlacePiece(sf::Vector2i pixel);
    void maybeMakeAiMove();
    void autoPlaceRandom();
    void updateHardModeObstacles();
    void processNetworkEvents();
    void startNetworkHost();
    void startNetworkJoin();
    void stopNetwork();
    bool isMyTurn() const;

    // Card event system
    void loadCardTextures();
    void generateHeartTextures();
    void generateWorldTextures();
    void generateStrengthTextures();
    void generateEmpressTextures();
    void generateHighPriestessTextures();
    void generateSunTextures();
    void generateStarTextures();
    void generateMoonTextures();
    void generateHermitTextures();
    void generateHierophantTextures();
    void generateJusticeTextures();
    void generateHangedManTextures();
    void generateDevilTextures();
    void generateChariotTextures();
    void triggerCardEvent();
    void applyCardEffect(CardType card);
    void handleCardSelectionClick(sf::Vector2f mousePos);

    void handleMainMenuClick(sf::Vector2f mousePosition);
    void handleRulesClick(sf::Vector2f mousePosition);
    void handleModeSelectClick(sf::Vector2f mousePosition);
    void handleDifficultySelectClick(sf::Vector2f mousePosition);
    void handleNetworkLobbyClick(sf::Vector2f mousePosition);
    void handleRoomSetupClick(sf::Vector2f mousePosition);
    void handleNetworkWaitClick(sf::Vector2f mousePosition);
    void handleGameClick(sf::Vector2f mousePosition);

    std::optional<sf::Vector2i> findBestAiMove() const;
    std::optional<sf::Vector2i> findEasyAiMove() const;
    std::optional<sf::Vector2i> findHardAiMove() const;
    int scoreMove(int row, int col, Board::Piece piece) const;
    int scoreDirection(int row, int col, int dRow, int dCol, Board::Piece piece) const;
    int countInDirection(int row, int col, int dRow, int dCol, Board::Piece piece) const;
    bool isOpenEnd(int row, int col) const;

    bool loadFont();
    void loadMenuBackground();
    void updateTitleParticles();
    void updateGameParticles();
    void updateSparks();
    void spawnSparks(sf::Vector2f pos, Board::Piece piece);
    void drawMainMenu();
    void drawRulesScene();
    void drawModeSelectScene();
    void drawDifficultySelectScene();
    void drawNetworkLobbyScene();
    void drawRoomSetupScene();
    void drawNetworkWaitScene();
    void drawGameScene();
    void drawButton(const UiButton& button, bool hovered, unsigned int characterSize, bool pressed = false);
    void drawText(std::string_view text,
                  sf::Vector2f position,
                  unsigned int characterSize,
                  sf::Color color,
                  std::uint32_t style = sf::Text::Regular);
    void drawCenteredText(std::string_view text,
                          const sf::FloatRect& area,
                          unsigned int characterSize,
                          sf::Color color,
                          std::uint32_t style = sf::Text::Regular);
    UiButton makeButton(sf::Vector2f position,
                        sf::Vector2f size,
                        std::string label,
                        sf::Color fill,
                        sf::Color accent) const;
    bool isMouseOver(const UiButton& button, sf::Vector2f mousePosition) const;

    std::string statusLine() const;
    std::string subtitleLine() const;
    void startCrossfade(sf::Music* target, float targetVol);
    void playPlaceSound();
    void playButtonSound();
    void updateWindowTitle();
    Board::Piece nextTurn(Board::Piece piece) const;
    std::string pieceName(Board::Piece piece) const;
    std::string modeName() const;
    std::string playerModeName() const;
    std::string difficultyName() const;

    struct TitleParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float rotation = 0.0f;
        float rotSpeed = 0.0f;
        sf::Color color;
        float swayPhase = 0.0f;
        float swayAmp = 0.0f;
    };

    struct Spark {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        sf::Color color;
        float size = 3.0f;
    };

    struct GameParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        sf::Color color;
        float size = 4.0f;
        float swayPhase = 0.0f;
        float swayAmp = 0.0f;
        float alphaPhase = 0.0f; // for firefly glow pulsing
    };

    sf::RenderWindow window_;
    sf::Font uiFont_;
    sf::Music bgMusic_;
    sf::Music easyMusic_;
    sf::Music mediumMusic_;
    sf::Music hardMusic_;
    sf::Music eventMusic_;
    sf::Music placeSfx_;
    sf::Music* currentMusic_ = nullptr;
    float currentMusicVol_ = 0.0f;
    sf::Music* outgoingMusic_ = nullptr;
    float outgoingStartVol_ = 0.0f;
    sf::Clock crossfadeClock_;
    static constexpr float kCrossfadeDuration = 1.2f;
    static constexpr float kEventCrossfadeDuration = 0.6f;
    CardEventState lastCardEventState_ = CardEventState::Idle;
    sf::Music* savedGameMusic_ = nullptr;
    float savedGameMusicVol_ = 0.0f;
    sf::SoundBuffer btnSoundBuffer_;
    std::optional<sf::Sound> btnSound_;
    sf::SoundBuffer winSoundBuffer_;
    std::optional<sf::Sound> winSound_;
    sf::SoundBuffer loseSoundBuffer_;
    std::optional<sf::Sound> loseSound_;
    bool winLoseTriggered_ = false;
    sf::Clock winLoseFadeClock_;
    static constexpr float kWinLoseFadeDuration = 0.5f;
    sf::Texture menuBgTex_;
    sf::Texture startBtnTex_;
    sf::Texture rulesBtnTex_;
    sf::Texture exitBtnTex_;
    sf::Texture modeBtnTex_;
    sf::Texture diffCardTex_;
    sf::Texture noiseTex_;
    sf::Texture gameBgTex_;
    sf::Texture networkBgTex_;
    sf::Texture rulesBgTex_;
    sf::Clock titleClock_;
    sf::Clock modeSelectClock_;
    sf::Clock buttonAnimClock_;
    int buttonAnimIndex_ = -1;
    TransitionState transitionState_ = TransitionState::None;
    float transitionAlpha_ = 0.0f;
    std::function<void()> transitionAction_;
    sf::Clock transitionClock_;
    static constexpr float kTransitionDuration = 0.4f;
    float gameIntroAlpha_ = 0.0f;
    sf::Clock gameIntroClock_;
    static constexpr float kGameIntroDuration = 0.65f;
    std::vector<GameParticle> gameParticles_;
    sf::Clock gameParticleClock_;
    std::vector<Spark> sparks_;
    float gameParticleTimer_ = 0.0f;
    sf::Clock atmosphereClock_;
    std::vector<TitleParticle> titleParticles_;
    Board board_;
    Scene scene_ = Scene::MainMenu;
    Scene lastScene_ = Scene::MainMenu;
    Board::Mode boardMode_ = Board::Mode::Classic;
    Board::Piece currentTurn_ = Board::Piece::Black;
    Board::Piece winner_ = Board::Piece::None;
    bool aiEnabled_ = true;
    AIDifficulty aiDifficulty_ = AIDifficulty::Medium;
    bool gameOver_ = false;
    bool fontLoaded_ = false;
    bool aiMovePending_ = false;
    sf::Clock aiClock_;
    int aiThinkTimeMs_ = 600;
    bool obstacleEventPending_ = false;
    sf::Clock obstacleEventClock_;
    int pendingSpawnAmount_ = 0;
    int pendingDespawnAmount_ = 0;
    int hardModeObstacleMoves_ = 0;
    int remainingUndos_ = 0;
    bool undoUsed_ = false;
    sf::Clock turnTimer_;
    int turnTimeLimit_ = 8;
    bool turnTimedOut_ = false;
    bool gameReady_ = false;

    std::unique_ptr<NetworkServer> server_;
    std::unique_ptr<NetworkClient> client_;
    int rulesPage_ = 0;
    static constexpr int kRulesTotalPages = 3;
    bool networkMode_ = false;
    bool isNetworkHost_ = false;
    bool networkDisconnected_ = false;
    unsigned short networkPort_ = 12345;
    std::string joinIp_;
    Board::Piece roomHostPiece_ = Board::Piece::Black;
    int roomUndoCount_ = 3;
    int roomTurnTime_ = 8;
    bool obstacleDynamic_ = false;
    int stepperAnimRow_ = -1;
    int stepperAnimDir_ = 0;
    int stepperAnimTarget_ = 0;
    sf::Clock stepperAnimClock_;
    std::vector<std::string> mapFiles_;
    int selectedMapIndex_ = -1;
    bool mapSelectOpen_ = false;
    bool ipInputActive_ = false;
    float ipCursorBlink_ = 0.0f;
    std::vector<TitleParticle> waitParticles_;
    sf::Clock waitParticleClock_;
    int mapSelectCurrentIdx_ = 0;
    sf::Texture mapPreviewTex_;

    // Opponent character sprite animation
    enum class OpponentAnim { Idle, Think, Win, Lose };
    sf::Texture opponentTex_;
    sf::Texture opponentThinkTex_;
    sf::Texture opponentWinTex_;
    sf::Texture opponentLoseTex_;
    OpponentAnim opponentAnim_ = OpponentAnim::Idle;
    int opponentIdleFrames_ = 0;
    int opponentThinkFrames_ = 0;
    int opponentWinFrames_ = 0;
    int opponentLoseFrames_ = 0;
    int opponentFrameWidth_ = 0;
    int opponentFrameHeight_ = 0;
    int opponentCurrentFrame_ = 0;
    sf::Clock opponentAnimClock_;
    float opponentFrameDuration_ = 0.15f;
    bool opponentLoseFinished_ = false;
    float opponentScale_ = 2.5f;
    float opponentOffsetX_ = 0.0f;

    // Speech bubble
    std::string speechText_;
    sf::Clock speechClock_;
    int speechMsgIndex_ = 0;
    bool speechIntroduced_ = false;

    // Card event system
    static constexpr int kNumCardTextures = 22;
    static constexpr int kCardsPerEvent = 3;
    static constexpr int kActiveCardCount = 22;
    CardEventState cardEventState_ = CardEventState::Idle;
    int nextCardThreshold_ = 10;  // Trigger at total pieces: 10, 18, 26, 34...
    std::array<CardType, kCardsPerEvent> drawnCards_{};
    int selectedCardIndex_ = -1;
    sf::Clock cardEventClock_;
    std::array<sf::Texture, kNumCardTextures> cardTextures_{};
    sf::Texture cardBackTex_;

    // Network wheel spin (determines who picks the card)
    sf::Clock wheelSpinClock_;
    float wheelAngle_ = 0.0f;
    float wheelStartAngle_ = 0.0f;
    bool wheelResultHost_ = false;      // true=host picks, false=guest picks
    bool wheelResultSet_ = false;       // result has been received/determined
    bool wheelStopped_ = false;         // wheel has stopped spinning
    bool iAmPicker_ = false;            // this player is the one who picks the card

    // Synced RNG seed for card effects (server-authoritative)
    std::uint32_t cardEffectSeed_ = 0;
    bool cardEffectSeedSet_ = false;

    // Active card effect trackers
    bool foolActive_ = false;
    int hierophantRemaining_ = 0;
    int temperanceRemaining_ = 0;
    int strengthProtectionRemaining_ = 0;
    sf::Vector2i strengthProtectedPos_{-1, -1};
    bool moonActive_ = false;
    sf::Vector2i starHighlightPos_{-1, -1};
    bool starHighlightValid_ = false;
    bool cardDeferredTurn_ = false;
    bool chariotPending_ = false;
    int hermitRemaining_ = 0;

    // Death card spread animation
    bool deathAnimPending_ = false;

    // Lovers card animation
    bool loversAnimPending_ = false;
    sf::Vector2i loversPieceA_{-1, -1};
    sf::Vector2i loversPieceB_{-1, -1};
    Board::Piece loversPieceAType_ = Board::Piece::None;
    sf::Clock loversAnimClock_;
    float loversArcProgress_ = 0.0f;
    std::vector<sf::Vector2f> loversArcPathA_;
    std::vector<sf::Vector2f> loversArcPathB_;
    sf::Vector2f loversMidpoint_{};
    float loversFloatingTextAlpha_ = 0.0f;
    sf::Vector2i deathCenter_{-1, -1};
    sf::Clock deathAnimClock_;
    int deathSpreadStep_ = 0;
    float deathFlashAlpha_ = 0.0f;
    float deathConsumeProgress_ = 0.0f;
    struct DeathPiece {
        sf::Vector2f pixelPos;
        Board::Piece piece;
    };
    std::vector<DeathPiece> deathPieces_;
    sf::Texture crownTex_;

    // Lovers card heart textures (procedurally generated)
    sf::Texture heartGlowTex_;
    sf::Texture heartSmallTex_;
    sf::Texture heartBurstTex_;
    bool heartTexturesLoaded_ = false;

    struct LoversHeartParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
        float rotation = 0.0f;
        float rotSpeed = 0.0f;
    };
    std::vector<LoversHeartParticle> loversHeartParticles_;

    // World card animation (Golden Mandala / Cycle Sealing)
    bool worldAnimPending_ = false;
    sf::Clock worldAnimClock_;
    sf::Texture mandalaTex_;
    sf::Texture goldSparkTex_;
    bool worldTexturesLoaded_ = false;
    float mandalaRotation_ = 0.0f;
    float mandalaScale_ = 0.0f;
    float mandalaAlpha_ = 0.0f;
    sf::Vector2f mandalaCenter_{640.0f, 350.0f};
    std::array<float, 5> ringRadii_{};
    std::array<float, 5> ringAlphas_{};
    float sealFlashAlpha_ = 0.0f;
    float amberVignetteAlpha_ = 0.0f;
    sf::Vector2f sealTarget_{996.0f, 550.0f};
    bool worldSealTriggered_ = false;

    struct WorldParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
        float rotation = 0.0f;
        float rotSpeed = 0.0f;
    };
    std::vector<WorldParticle> worldParticles_;

    // Strength card animation (Power Infusion)
    bool strengthAnimPending_ = false;
    sf::Clock strengthAnimClock_;
    sf::Texture strengthRuneTex_;
    sf::Texture strengthSparkTex_;
    bool strengthTexturesLoaded_ = false;
    float strengthFlashAlpha_ = 0.0f;
    float strengthShockwaveRadius_ = 0.0f;
    float strengthShockwaveAlpha_ = 0.0f;

    struct StrengthParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
    };
    std::vector<StrengthParticle> strengthParticles_;

    // Empress card animation (Blossom Growth)
    bool empressAnimPending_ = false;
    bool empressPiecePlaced_ = false;
    sf::Vector2i empressSourcePiece_{-1, -1};
    sf::Vector2i empressTargetCell_{-1, -1};
    Board::Piece empressPlayerPiece_ = Board::Piece::None;
    sf::Clock empressAnimClock_;
    float empressVineProgress_ = 0.0f;
    float empressBloomPhase_ = 0.0f;

    struct EmpressPetal {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float rotation = 0.0f;
        float rotSpeed = 0.0f;
        float scale = 1.0f;
        float swayPhase = 0.0f;
        float swayAmp = 0.0f;
        int colorType = 0;
    };
    std::vector<EmpressPetal> empressPetals_;

    struct EmpressFirefly {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float glowPhase = 0.0f;
        float baseRadius = 0.0f;
        float orbitAngle = 0.0f;
        float orbitSpeed = 0.0f;
    };
    std::vector<EmpressFirefly> empressFireflies_;

    sf::Texture petalTex_;
    sf::Texture bloomGlowTex_;
    bool empressTexturesLoaded_ = false;

    // High Priestess card animation (Sanctuary Cross)
    bool highPriestessAnimPending_ = false;
    sf::Vector2i hpCrossCenter_{-1, -1};
    sf::Clock hpAnimClock_;
    float hpPillarAlpha_ = 0.0f;
    float hpPillarYOffset_ = 0.0f;
    float hpBeamProgress_ = 0.0f;
    float hpCrescentAlpha_ = 0.0f;
    bool hpPiecesRemoved_ = false;

    struct HpMote {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
    };
    std::vector<HpMote> hpMotes_;

    struct HpDissolvingPiece {
        sf::Vector2f pixelPos;
        Board::Piece piece;
        float dissolveProgress = 0.0f;
    };
    std::vector<HpDissolvingPiece> hpDissolvingPieces_;

    sf::Texture pillarGlowTex_;
    sf::Texture hpMoteTex_;
    bool hpTexturesLoaded_ = false;

    // Sun card animation (Radiant Descent)
    bool sunAnimPending_ = false;
    sf::Clock sunAnimClock_;
    float sunDiscScale_ = 0.0f;
    float sunDiscAlpha_ = 0.0f;
    float sunDiscYOffset_ = 0.0f;
    float sunRotation_ = 0.0f;
    float sunRayAlpha_ = 0.0f;
    float sunZoneGlowAlpha_ = 0.0f;
    float sunShockwaveRadius_ = 0.0f;
    float sunShockwaveAlpha_ = 0.0f;

    struct SunMote {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
    };
    std::vector<SunMote> sunMotes_;

    sf::Texture sunDiscTex_;
    sf::Texture sunRayTex_;
    sf::Texture sunMoteTex_;
    bool sunTexturesLoaded_ = false;

    // Star card animation (Guiding Star)
    bool starAnimPending_ = false;
    bool starInPersistentMode_ = false;
    sf::Clock starAnimClock_;
    float starYOffset_ = 0.0f;
    float starScale_ = 0.0f;
    float starAlpha_ = 0.0f;
    float starRotation_ = 0.0f;
    float starRayAlpha_ = 0.0f;

    struct StarDust {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
        float rotation = 0.0f;
        float rotSpeed = 0.0f;
    };
    std::vector<StarDust> starDustParticles_;

    sf::Texture starSpriteTex_;
    sf::Texture starDustTex_;
    bool starTexturesLoaded_ = false;

    // Moon card animation (Lunar Mist)
    bool moonAnimPending_ = false;
    sf::Clock moonAnimClock_;
    float moonAlpha_ = 0.0f;
    float moonYOffset_ = 0.0f;
    float moonMistAlpha_ = 0.0f;
    float moonOverlayAlpha_ = 0.0f;

    struct MoonMistParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
    };
    std::vector<MoonMistParticle> moonMistParticles_;

    sf::Texture moonTex_;
    sf::Texture moonMistTex_;
    bool moonTexturesLoaded_ = false;
    sf::Vector2i moonOriginalPos_{-1, -1};
    float moonMarkerAlpha_ = 0.0f;

    // Hermit card animation (Mirror-Still Water)
    bool hermitAnimPending_ = false;
    sf::Clock hermitAnimClock_;
    float hermitOverlayAlpha_ = 0.0f;
    float hermitPondRadius_ = 0.0f;
    float hermitPondAlpha_ = 0.0f;
    float hermitRingAlpha_ = 0.0f;

    struct HermitLantern {
        sf::Vector2f pos;
        float angle = 0.0f;
        float orbitRadius = 0.0f;
        float orbitSpeed = 0.0f;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
        float glowPhase = 0.0f;
    };
    std::vector<HermitLantern> hermitLanterns_;

    struct HermitRipple {
        float radius = 0.0f;
        float alpha = 0.0f;
        float maxRadius = 0.0f;
        float speed = 0.0f;
    };
    std::vector<HermitRipple> hermitRipples_;

    sf::Texture hermitPondTex_;
    sf::Texture hermitLanternTex_;
    bool hermitTexturesLoaded_ = false;

    // Hierophant card animation (Rule Binding)
    bool hierophantAnimPending_ = false;
    sf::Clock hierophantAnimClock_;
    float hierophantLightAlpha_ = 0.0f;
    float hierophantBoundaryAlpha_ = 0.0f;
    float hierophantSealAlpha_ = 0.0f;
    float hierophantOuterDimAlpha_ = 0.0f;
    float hierophantCornerAlpha_ = 0.0f;

    struct HierophantParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
    };
    std::vector<HierophantParticle> hierophantParticles_;

    sf::Texture hierophantLightTex_;
    sf::Texture hierophantRuneTex_;
    bool hierophantTexturesLoaded_ = false;

    // Justice card animation (Heavenly Cycle)
    bool justiceAnimPending_ = false;
    sf::Clock justiceAnimClock_;
    sf::Vector2i justiceBlackPiece_{-1, -1};
    sf::Vector2i justiceWhitePiece_{-1, -1};
    float justiceScaleAlpha_ = 0.0f;
    float justiceLightAlpha_ = 0.0f;
    float justiceScreenBrightAlpha_ = 0.0f;
    float justicePieceGlowAlpha_ = 0.0f;
    bool justicePiecesRemoved_ = false;

    struct JusticeFeather {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
        float rotation = 0.0f;
        float swayPhase = 0.0f;
        float swayAmp = 0.0f;
    };
    std::vector<JusticeFeather> justiceFeathers_;

    sf::Texture justiceScaleTex_;
    sf::Texture justiceFeatherTex_;
    bool justiceTexturesLoaded_ = false;

    // Hanged Man card animation (Self-Sacrifice)
    bool hangedManAnimPending_ = false;
    sf::Clock hangedManAnimClock_;
    sf::Vector2i hangedManSacrificePos_{-1, -1};
    sf::Vector2i hangedManTargetA_{-1, -1};
    sf::Vector2i hangedManTargetB_{-1, -1};
    float hangedManOverlayAlpha_ = 0.0f;
    float hangedManSacrificeGlowAlpha_ = 0.0f;
    float hangedManThreadProgress_ = 0.0f;
    float hangedManTargetGlowAlpha_ = 0.0f;
    float hangedManMarkAlpha_ = 0.0f;
    bool hangedManSacrificed_ = false;
    bool hangedManTargetsRemoved_ = false;

    struct HangedManParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
    };
    std::vector<HangedManParticle> hangedManParticles_;

    sf::Texture hangedManMarkTex_;
    bool hangedManTexturesLoaded_ = false;

    // Devil card animation (Demon Contract)
    bool devilAnimPending_ = false;
    sf::Clock devilAnimClock_;
    sf::Vector2i devilTargetPos_{-1, -1};
    float devilOverlayAlpha_ = 0.0f;
    float devilPentagramAngle_ = 0.0f;
    float devilPentagramAlpha_ = 0.0f;
    float devilPentagramScale_ = 1.0f;
    float devilFlameIntensity_ = 0.0f;
    float devilScorchAlpha_ = 0.0f;
    bool devilPieceRemoved_ = false;

    struct DevilParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
        bool isEmber = false;  // true = ember ash, false = dark flame
    };
    std::vector<DevilParticle> devilParticles_;

    sf::Texture devilPentagramTex_;
    bool devilTexturesLoaded_ = false;

    // Chariot card animation (Charge & Push)
    bool chariotAnimPending_ = false;
    sf::Clock chariotAnimClock_;
    sf::Vector2i chariotPushSource_{-1, -1};
    sf::Vector2i chariotPushDest_{-1, -1};
    sf::Vector2i chariotPlayerPos_{-1, -1};
    float chariotOverlayAlpha_ = 0.0f;
    float chariotChargeAlpha_ = 0.0f;
    float chariotGearAngle_ = 0.0f;
    float chariotImpactAlpha_ = 0.0f;
    float chariotTrailAlpha_ = 0.0f;
    bool chariotPiecePushed_ = false;
    bool chariotPiecePlaced_ = false;

    struct ChariotParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
    };
    std::vector<ChariotParticle> chariotParticles_;

    sf::Texture chariotGearTex_;
    bool chariotTexturesLoaded_ = false;

    // Magician card animation (Mirror Image)
    bool magicianAnimPending_ = false;
    sf::Clock magicianAnimClock_;
    sf::Vector2i magicianSourcePos_{-1, -1};
    sf::Vector2i magicianTargetPos_{-1, -1};
    float magicianOverlayAlpha_ = 0.0f;
    float magicianRippleAlpha_ = 0.0f;
    float magicianArcProgress_ = 0.0f;
    float magicianPortalAlpha_ = 0.0f;
    float magicianSpawnAlpha_ = 0.0f;
    bool magicianPiecePlaced_ = false;

    struct MagicianParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
        int type = 0; // 0 = diamond, 1 = star/sparkle
    };
    std::vector<MagicianParticle> magicianParticles_;

    // Temperance card animation (Harmony)
    bool temperanceAnimPending_ = false;
    sf::Clock temperanceAnimClock_;
    float temperanceOverlayAlpha_ = 0.0f;
    float temperanceWingAlpha_ = 0.0f;
    float temperanceHaloRadius_ = 0.0f;
    float temperanceHaloAlpha_ = 0.0f;
    float temperanceMarkAlpha_ = 0.0f;
    float temperanceMarkAngle_ = 0.0f;

    struct TemperanceParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
    };
    std::vector<TemperanceParticle> temperanceParticles_;

    // Fool card animation (Lucky Star)
    bool foolAnimPending_ = false;
    sf::Clock foolAnimClock_;
    sf::Vector2f foolStarPos_{640.0f, 300.0f};
    float foolStarBounceT_ = 0.0f;
    float foolOverlayAlpha_ = 0.0f;
    float foolStarAlpha_ = 0.0f;
    float foolStarAngle_ = 0.0f;
    float foolStarScale_ = 1.0f;
    float foolBurstAlpha_ = 0.0f;

    struct FoolParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
        float hue = 0.0f; // rainbow hue 0~1
    };
    std::vector<FoolParticle> foolParticles_;

    // Tower card animation (Cataclysm)
    bool towerAnimPending_ = false;
    sf::Clock towerAnimClock_;
    int towerObstacleCount_ = 0;
    float towerFlashAlpha_ = 0.0f;
    float towerLightningAlpha_ = 0.0f;
    float towerRubbleAlpha_ = 0.0f;
    float towerRebirthAlpha_ = 0.0f;
    bool towerObstaclesCleared_ = false;
    bool towerObstaclesRegenerated_ = false;
    std::vector<sf::Vector2f> towerSavedObstacles_;

    struct TowerParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
        bool isRubble = false;
    };
    std::vector<TowerParticle> towerParticles_;

    // Wheel of Fortune card animation (Fate's Roulette)
    bool wheelFortuneAnimPending_ = false;
    sf::Clock wheelFortuneAnimClock_;
    int wheelFortuneObstacleCount_ = 0;
    float wheelFortuneAngle_ = 0.0f;
    float wheelFortuneAlpha_ = 0.0f;
    float wheelFortuneRadius_ = 0.0f;
    float wheelFortuneOrbProgress_ = 0.0f;
    bool wheelFortuneObstaclesCleared_ = false;
    bool wheelFortuneObstaclesRegenerated_ = false;
    std::vector<sf::Vector2f> wheelFortuneSavedObstacles_;

    struct WheelFortuneParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float scale = 1.0f;
    };
    std::vector<WheelFortuneParticle> wheelFortuneParticles_;

    // Judgement card animation (Final Judgment — divine light purification)
    bool judgementAnimPending_ = false;
    sf::Clock judgementAnimClock_;
    float judgementLightBeamProgress_ = 0.0f;
    float judgementDarkenAlpha_ = 0.0f;
    bool judgementObstaclesCleared_ = false;
    std::vector<sf::Vector2f> judgementSavedObstacles_;

    struct JudgementParticle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float life = 0.0f;
        float maxLife = 0.0f;
        float size = 1.0f;
    };
    std::vector<JudgementParticle> judgementParticles_;

    struct JudgementRing {
        float radius = 0.0f;
        float maxRadius = 0.0f;
        float alpha = 0.0f;
    };
    std::vector<JudgementRing> judgementRings_;

    // Obstacle removal animation (generic dissolve + Emperor golden banish)
    struct ObstacleRemovalAnim {
        sf::Vector2f pixelPos;
        sf::Vector2i gridPos;
        float elapsed = 0.0f;
        bool emperorStyle = false;
    };
    std::vector<ObstacleRemovalAnim> obstacleRemovalAnims_;

    // Card event animation state
    sf::Texture messengerTex_;
    float messengerAlpha_ = 0.0f;
    sf::Vector2f messengerPos_{640.0f, 410.0f};
    float shakeIntensity_ = 0.0f;
    float darkenAlpha_ = 0.0f;
    float cardDealProgress_ = 0.0f;
    float cardFlipProgress_ = 0.0f;
    float cardFloatTime_ = 0.0f;
    float cardRevealProgress_ = 0.0f;
    int chosenCardIdx_ = -1;
    std::string cardEventSpeech_;
    sf::Clock messengerFloatClock_;
};
