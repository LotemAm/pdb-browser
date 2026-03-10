#pragma once
struct AppState;

class BrowserPanel {
public:
    void render(AppState* state);

private:
    char m_filterBuf[256] = "";
};
