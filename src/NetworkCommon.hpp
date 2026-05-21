#pragma once

struct NetMove {
    int row;
    int col;
};

struct RoomConfig {
    int mode = 0;       // 0=Classic, 1=Obstacle
    int undoCount = 3;
    int turnTime = 8;
    int selectedMapIndex = -1;
    int obstacleDynamic = 0;
    bool received = false;
};
