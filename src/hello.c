#include <SDL3/SDL.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <pthread.h>

int WINDOW_WIDTH = 800;
int WINDOW_HEIGHT = 800;

float RE_MIN = -2.0f;
float RE_MAX = 2.0f;
float IM_MIN = -2.0f;
float IM_MAX = 2.0f;
double CENTER_X = -0.5;
double CENTER_Y = 0.0;
double ZOOM = 1.0;
bool dragging = false;
bool zooming = false;
int dragStartX = 0, dragStartY = 0;
double dragOriginCenterX = 0.0, dragOriginCenterY = 0.0;
int iterations = 100;

typedef struct {
    double real, imag;
} Complex;

typedef struct {
  int startY, endY;
  int width;
  int height;
  int iterations;
  double centerX, centerY, zoom;
  Uint32* framebuffer;
} ThreadData;



Complex complexSquare(Complex z) {
    Complex result;
    result.real = z.real * z.real - z.imag * z.imag;
    result.imag = 2 * z.real * z.imag;
    return result;
}

double complexMagnitudeSquared(Complex z) {
    return z.real * z.real + z.imag * z.imag;
}

Complex map(int px, int py) {
    Complex p;
    double scale = 1.0 / ZOOM;
    p.real = CENTER_X + (px - WINDOW_WIDTH / 2.0) * (4.0 * scale / WINDOW_WIDTH);
    p.imag = CENTER_Y + (py - WINDOW_HEIGHT / 2.0) * (4.0 * scale / WINDOW_HEIGHT);
    return p;
}

void HSVtoRGB(float h, float s, float v, Uint8* r, Uint8* g, Uint8* b) {
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = v - c;
    float r1, g1, b1;

    if (h < 60)      { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120){ r1 = x; g1 = c; b1 = 0; }
    else if (h < 180){ r1 = 0; g1 = c; b1 = x; }
    else if (h < 240){ r1 = 0; g1 = x; b1 = c; }
    else if (h < 300){ r1 = x; g1 = 0; b1 = c; }
    else             { r1 = c; g1 = 0; b1 = x; }

    *r = (r1 + m) * 255;
    *g = (g1 + m) * 255;
    *b = (b1 + m) * 255;
}

void* render_mandelbrot(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    int startY = data->startY;
    int endY = data->endY;
    int width = data->width;
    int height = data->height;
    int iterations = data->iterations;
    double centerX = data->centerX;
    double centerY = data->centerY;
    double zoom = data->zoom;
    Uint32* framebuffer = data->framebuffer;

    int renderSteps = (dragging || zooming) ? 2 : 1;

    for (int y = startY; y < endY; y += renderSteps) {
        for (int x = 0; x < width; x += renderSteps) {
            // Map pixel to complex plane
            double scale = 1.0 / zoom;
            double real = centerX + (x - width / 2.0) * (4.0 * scale / width);
            double imag = centerY + (y - height / 2.0) * (4.0 * scale / height);

            Complex c = {real, imag};
            Complex z = {0.0, 0.0};
            int i;
            for (i = 0; i < iterations; i++) {
                z = complexSquare(z);
                z.real += c.real;
                z.imag += c.imag;
                if (complexMagnitudeSquared(z) > 4.0) break;
            }

            Uint8 r, g, b;
            if (i == iterations) {
                r = g = b = 0;
            } else {
                float hue = 360.0f * i / iterations;
                HSVtoRGB(hue, 1.0f, 1.0f, &r, &g, &b);
            }
            framebuffer[y * width + x] = (255 << 24) | (r << 16) | (g << 8) | b;
        }
    }

    return NULL;
}

int main() {
    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window* window = SDL_CreateWindow("Mandelbrot", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WINDOW_WIDTH, WINDOW_HEIGHT);

    Uint32* framebuffer = malloc(WINDOW_WIDTH * WINDOW_HEIGHT * sizeof(Uint32));

    bool running = true;
    SDL_Event event;

    
    #define NUM_THREADS 4

pthread_t threads[NUM_THREADS];
ThreadData threadData[NUM_THREADS];

while (running) {
    while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                dragging = true;
                dragStartX = event.button.x;
                dragStartY = event.button.y;
                dragOriginCenterX = CENTER_X;
                dragOriginCenterY = CENTER_Y;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
                dragging = false;
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION && dragging) {
                int dx = event.motion.x - dragStartX;
                int dy = event.motion.y - dragStartY;
                double scale = 4.0 / ZOOM;
                CENTER_X = dragOriginCenterX - (dx * scale / WINDOW_WIDTH);
                CENTER_Y = dragOriginCenterY - (dy * scale / WINDOW_HEIGHT);
            }

            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                if (event.wheel.y > 0) ZOOM *= 1.1;
                if (event.wheel.y < 0) ZOOM /= 1.1;
                zooming = true;
            }else {
              zooming = false;
            }

            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_1) iterations += 50;
                else if (event.key.key == SDLK_0 && iterations > 50) iterations -= 50;
            }
        }

    // Clear framebuffer
    memset(framebuffer, 0, WINDOW_WIDTH * WINDOW_HEIGHT * sizeof(Uint32));

    int rowsPerThread = WINDOW_HEIGHT / NUM_THREADS;

    for (int i = 0; i < NUM_THREADS; i++) {
        threadData[i].startY = i * rowsPerThread;
        threadData[i].endY = (i == NUM_THREADS - 1) ? WINDOW_HEIGHT : (i + 1) * rowsPerThread;
        threadData[i].width = WINDOW_WIDTH;
        threadData[i].height = WINDOW_HEIGHT;
        threadData[i].iterations = iterations;
        threadData[i].centerX = CENTER_X;
        threadData[i].centerY = CENTER_Y;
        threadData[i].zoom = ZOOM;
        threadData[i].framebuffer = framebuffer;

        pthread_create(&threads[i], NULL, render_mandelbrot, &threadData[i]);
    }

    // Wait for all threads to finish
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    SDL_UpdateTexture(texture, NULL, framebuffer, WINDOW_WIDTH * sizeof(Uint32));
    SDL_RenderTexture(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

    free(framebuffer);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
