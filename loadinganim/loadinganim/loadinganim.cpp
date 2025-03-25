#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <chrono>
#include <cmath>
#include <algorithm>

#pragma comment (lib, "gdiplus.lib")

using namespace Gdiplus;
using namespace std::chrono;

// -----------------------------------------------------------------------------
// Enum und Hilfsfunktionen
// -----------------------------------------------------------------------------
enum MovementPhase { ToRight, WaitRight, ToCenter };

float Lerp(float start, float end, float t) {
    return start + (end - start) * t;
}

// -----------------------------------------------------------------------------
// Klasse LoginWindow – implementiert das Layered Window mit Animationen
// -----------------------------------------------------------------------------
class LoginWindow {
public:
    HWND hwnd;

    // Animationseinstellungen
    Bitmap* image; // Zeiger auf das zu ladende Bild
    float fadeProgress;         // 0 = unsichtbar, 1 = voll sichtbar
    float textRevealProgress;   // 0 = nichts sichtbar, 1 = kompletter Text
    float loadingBarProgress;   // 0 = leer, 1 = voll
    bool loadingBarAnimationStarted;
    bool loadingBarDelayStarted;
    time_point<steady_clock> loadingBarDelayStart;

    // Text-Einstellungen
    std::wstring fullText;
    Font* textFont;
    int textMargin;

    // Bildbewegung
    float currentImageCenterX;
    MovementPhase movementPhase;
    time_point<steady_clock> waitStartTime;
    milliseconds waitDuration;

    // Anwendungseinstellungen
    int appWidth;
    float scale;

    // Maus-Dragging
    bool isDragging;
    POINT dragStartPoint;
    POINT windowStartPoint;

    LoginWindow()
        : hwnd(nullptr), image(nullptr), fadeProgress(0.f), textRevealProgress(0.f),
        loadingBarProgress(0.f), loadingBarAnimationStarted(false), loadingBarDelayStarted(false),
        fullText(L"YCATSCE"), textMargin(7),
        currentImageCenterX(0.f), movementPhase(ToRight), waitDuration(500),
        appWidth(800), scale(0.25f), isDragging(false)
    {
        // Erzeuge den Font (70px, Bold)
        textFont = new Font(L"Segoe UI", 70, FontStyleBold, UnitPixel);
    }

    ~LoginWindow() {
        if (image) delete image;
        if (textFont) delete textFont;
    }

    // Aktualisiert die Position des Bildes in Abhängigkeit von der aktuellen Phase
    void UpdateImageMovement() {
        if (!image) return;
        int scaledImageWidth = static_cast<int>(image->GetWidth() * scale);
        if (movementPhase == ToRight) {
            // Ziel: Bild fährt nach rechts (rechte Bildkante liegt leicht außerhalb des Fensters)
            float targetCenter = appWidth - scaledImageWidth * 1.3f;
            currentImageCenterX = Lerp(currentImageCenterX, targetCenter, 0.1f);
            if (fabs(currentImageCenterX - targetCenter) < 2.f) {
                movementPhase = WaitRight;
                waitStartTime = steady_clock::now();
            }
        }
        else if (movementPhase == WaitRight) {
            if (steady_clock::now() - waitStartTime >= waitDuration)
                movementPhase = ToCenter;
        }
        else if (movementPhase == ToCenter) {
            // Ermittle Textbreite über GDI+
            HDC hdcScreen = GetDC(NULL);
            Graphics g(hdcScreen);
            RectF boundingBox;
            g.MeasureString(fullText.c_str(), -1, textFont, RectF(0, 0, 0, 0), &boundingBox);
            float textWidth = boundingBox.Width;
            ReleaseDC(NULL, hdcScreen);
            float textStartX = (appWidth - textWidth) / 2.f;
            float targetCenter = textStartX + textWidth / 2.f;
            currentImageCenterX = Lerp(currentImageCenterX, targetCenter, 0.1f);
        }
    }

    // Wird vom FadeIn‑Timer (alle 20ms) aufgerufen
    void FadeInEffect() {
        fadeProgress = min(1.f, fadeProgress + 0.017f);
        textRevealProgress = min(1.f, textRevealProgress + 0.045f);

        UpdateImageMovement();

        // Starte die Loading-Bar Animation, sobald alle anderen Animationen abgeschlossen sind
        if (fadeProgress >= 1.f && textRevealProgress >= 1.f && movementPhase == ToCenter) {
            if (!loadingBarDelayStarted) {
                loadingBarDelayStart = steady_clock::now();
                loadingBarDelayStarted = true;
            }
            else if (steady_clock::now() - loadingBarDelayStart >= seconds(2) && !loadingBarAnimationStarted) {
                loadingBarAnimationStarted = true;
                // Startet den Timer für die Loading-Bar (ID 2, 20ms Intervall)
                SetTimer(hwnd, 2, 20, NULL);
            }
        }
        UpdateLayeredBitmap();
    }

    // Wird vom Loading-Bar Timer (alle 20ms) aufgerufen
    void LoadingBarEffect() {
        loadingBarProgress = min(1.f, loadingBarProgress + 0.01f);
        if (loadingBarProgress >= 1.f) {
            KillTimer(hwnd, 2);
            // Sobald die Loading-Bar voll ist, schließt sich das Fenster
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
        UpdateLayeredBitmap();
    }

    // Erzeugt ein offscreen Bitmap, zeichnet Bild, Text und Loading-Bar und aktualisiert
    // das Layered Window mittels UpdateLayeredWindow.
    void UpdateLayeredBitmap() {
        if (!image) return;
        int scaledImageWidth = static_cast<int>(image->GetWidth() * scale);
        int scaledImageHeight = static_cast<int>(image->GetHeight() * scale);

        // Ermittle die Textgröße über GDI+
        HDC hdcScreen = GetDC(NULL);
        Graphics graphics(hdcScreen);
        RectF boundingBox;
        graphics.MeasureString(fullText.c_str(), -1, textFont, PointF(0, 0), &boundingBox);
        float textWidth = boundingBox.Width;
        float textHeight = boundingBox.Height;
        ReleaseDC(NULL, hdcScreen);

        int baseHeight = scaledImageHeight + textMargin + static_cast<int>(ceil(textHeight));
        int loadingBarMargin = 10;
        int loadingBarHeight = 5;
        int newHeight = baseHeight + loadingBarMargin + loadingBarHeight;
        int newWidth = max(appWidth, static_cast<int>(ceil(textWidth)));

        // Erzeuge ein GDI+ Bitmap als offscreen Puffer
        Bitmap bmp(newWidth, newHeight, PixelFormat32bppARGB);
        {
            Graphics g(&bmp);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.SetTextRenderingHint(TextRenderingHintAntiAlias);

            // Hintergrund transparent füllen
            g.Clear(Color(0, 0, 0, 0));

            // --- Bild zeichnen mit Fade-In ---
            int imageX = static_cast<int>(currentImageCenterX - scaledImageWidth / 2.f);
            int imageY = 0;
            ColorMatrix cm = {
                1, 0, 0, 0, 0,
                0, 1, 0, 0, 0,
                0, 0, 1, 0, 0,
                0, 0, 0, fadeProgress, 0,
                0, 0, 0, 0, 1
            };
            ImageAttributes imgAttr;
            imgAttr.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
            Rect destRect(imageX, imageY, scaledImageWidth, scaledImageHeight);
            g.DrawImage(image, destRect, 0, 0, image->GetWidth(), image->GetHeight(), UnitPixel, &imgAttr);

            // --- Text zeichnen (mit Clipping für Reveal) ---
            float textStartX = (appWidth - textWidth) / 2.f;
            float textY = scaledImageHeight + textMargin;
            // Clipping-Region: von links bis zum Fortschritt
            Region clipRegion(RectF(textStartX, textY, textWidth * textRevealProgress, textHeight));
            g.SetClip(&clipRegion);
            // Zeichne „Glüheffekt“ (Shadow)
            SolidBrush glowBrush(Color(static_cast<BYTE>(255 * fadeProgress / 2), 255, 0, 255));
            g.DrawString(fullText.c_str(), -1, textFont, PointF(textStartX + 7, textY + 4), &glowBrush);
            // Zeichne den Haupttext
            SolidBrush textBrush(Color(static_cast<BYTE>(255 * fadeProgress), 255, 255, 255));
            g.DrawString(fullText.c_str(), -1, textFont, PointF(textStartX + 5, textY), &textBrush);
            g.ResetClip();

            // --- Loading-Bar zeichnen ---
            if (loadingBarAnimationStarted || loadingBarProgress > 0.f) {
                float loadingBarY = textY + textHeight - 25 + loadingBarMargin;
                float loadingBarX = textStartX;
                float loadingBarWidth = textWidth;
                float loadingBarHeight = 10.0f;  // Beispielhöhe

                // Rahmen zeichnen
                Gdiplus::Pen neonPen(Gdiplus::Color::DarkMagenta, 1);  // Pen mit Farbe und Dicke
                g.DrawRectangle(&neonPen, loadingBarX, loadingBarY, loadingBarWidth, loadingBarHeight);

                // Füllung der Loading-Bar
                Gdiplus::SolidBrush neonBrush(Gdiplus::Color::Magenta);  // Brush mit Füllfarbe
                g.FillRectangle(&neonBrush, loadingBarX, loadingBarY, loadingBarWidth * loadingBarProgress, loadingBarHeight);
            }

        }

        // Um das Layered Window zu aktualisieren, erstellen wir ein HBITMAP aus dem GDI+ Bitmap
        HDC hScreenDC = GetDC(NULL);
        HDC hMemDC = CreateCompatibleDC(hScreenDC);
        HBITMAP hBitmap;
        bmp.GetHBITMAP(Color(0, 0, 0, 0), &hBitmap);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);

        SIZE size = { newWidth, newHeight };
        POINT ptSrc = { 0, 0 };
        RECT wndRect;
        GetWindowRect(hwnd, &wndRect);
        POINT ptWndPos = { wndRect.left, wndRect.top };

        BLENDFUNCTION blend = { 0 };
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;

        UpdateLayeredWindow(hwnd, hScreenDC, &ptWndPos, &size, hMemDC, &ptSrc, 0, &blend, ULW_ALPHA);

        SelectObject(hMemDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemDC);
        ReleaseDC(NULL, hScreenDC);
    }
};

// -----------------------------------------------------------------------------
// Fensterprozedur
// -----------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LoginWindow* pThis;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<LoginWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->hwnd = hwnd;
    }
    else {
        pThis = reinterpret_cast<LoginWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
    case WM_CREATE:
        // Starte den FadeIn Timer (ID 1, 20ms Intervall)
        SetTimer(hwnd, 1, 20, NULL);
        break;
    case WM_TIMER:
        if (wParam == 1) {
            pThis->FadeInEffect();
        }
        else if (wParam == 2) {
            pThis->LoadingBarEffect();
        }
        break;
    case WM_LBUTTONDOWN:
    {
        pThis->isDragging = true;
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        pThis->dragStartPoint = pt;
        RECT rect;
        GetWindowRect(hwnd, &rect);
        pThis->windowStartPoint.x = rect.left;
        pThis->windowStartPoint.y = rect.top;
    }
    break;
    case WM_MOUSEMOVE:
        if (pThis->isDragging) {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            int dx = pt.x - pThis->dragStartPoint.x;
            int dy = pt.y - pThis->dragStartPoint.y;
            SetWindowPos(hwnd, NULL, pThis->windowStartPoint.x + dx, pThis->windowStartPoint.y + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            pThis->UpdateLayeredBitmap();
        }
        break;
    case WM_LBUTTONUP:
        pThis->isDragging = false;
        pThis->UpdateLayeredBitmap();
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
    }
    break;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// -----------------------------------------------------------------------------
// WinMain – Einstiegspunkt der Anwendung
// -----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // GDI+ initialisieren
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // Fensterklasse registrieren
    const wchar_t CLASS_NAME[] = L"LoginWindowClass";
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    // Da wir ein Layered Window nutzen, keinen Hintergrundpinsel setzen
    RegisterClass(&wc);

    // Instanz unserer LoginWindow-Klasse erzeugen
    LoginWindow login;
    HWND hwnd = CreateWindowEx(WS_EX_LAYERED, CLASS_NAME, L"Login", WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, login.appWidth, 600,
        NULL, NULL, hInstance, &login);
    if (hwnd == NULL)
        return 0;

    // Fenster zentrieren
    RECT rect;
    GetWindowRect(hwnd, &rect);
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - (rect.right - rect.left)) / 2;
    int y = (screenHeight - (rect.bottom - rect.top)) / 2;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Bild laden
    std::wstring imagePath = L"bild2.png";
    login.image = Bitmap::FromFile(imagePath.c_str());
    if (login.image == NULL || login.image->GetLastStatus() != Ok) {
        MessageBox(NULL, L"Bild nicht gefunden!", L"Fehler", MB_ICONERROR);
        return 0;
    }
    int scaledImageWidth = static_cast<int>(login.image->GetWidth() * login.scale);
    login.currentImageCenterX = scaledImageWidth / 2.f;

    // Nachrichten-Schleife
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    return 0;
}
