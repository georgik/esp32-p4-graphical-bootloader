#include "raylib.h"
#include "board_init.h"
#include "esp_raylib_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include "esp_lcd_touch.h"
#include "bsp/touch.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

#define TAG "RaylibDemo"
#define RAYLIB_TASK_STACK_SIZE (128 * 1024)  // 128KB stack for software renderer

// Tile configuration
#define TILE_COUNT 8
#define TILE_COLS 4
#define TILE_ROWS 2
#define TILE_MARGIN 20
#define TILE_WIDTH 120
#define TILE_HEIGHT 80

typedef struct {
    Rectangle rect;
    const char* label;
    Color color;
    bool isHovered;
    bool isPressed;
    bool isSelected;
    float selectionAnimation;
    int selectionTime;
    int otaIndex;  // Which OTA partition this tile represents (-1 for info)
} Tile;

// OTA switching function - similar to the LVGL bootloader
static void ota_switch_to_app(int app_index) {
    ESP_LOGI(TAG, "Attempting to switch to OTA partition %d", app_index);

    // Initially assume the first OTA partition, which is typically 'ota_0'
    const esp_partition_t *next_partition = esp_ota_get_next_update_partition(NULL);

    // Iterate to find the correct OTA partition only if app_index is greater than 0
    if (app_index > 0 && app_index <= 7) {  // We have 8 OTA partitions (0-7)
        for (int i = 0; i < app_index; i++) {
            next_partition = esp_ota_get_next_update_partition(next_partition);
            if (!next_partition) {
                ESP_LOGE(TAG, "Failed to get next OTA partition at iteration %d", i);
                break;
            }
        }
    }

    // For app_index 0, next_partition will not change, thus pointing to 'ota_0'
    if (next_partition && esp_ota_set_boot_partition(next_partition) == ESP_OK) {
        ESP_LOGI(TAG, "Successfully set boot partition to %s (%s)",
                 next_partition->label ? next_partition->label : "unknown",
                 next_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? "ota_0" :
                 next_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1 ? "ota_1" :
                 next_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_2 ? "ota_2" :
                 next_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_3 ? "ota_3" :
                 next_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_4 ? "ota_4" :
                 next_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_5 ? "ota_5" :
                 next_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_6 ? "ota_6" :
                 next_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_7 ? "ota_7" : "unknown");

        // Add a small delay to allow logging to complete
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "Restarting now to boot from the new partition...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Failed to set boot partition for app_index %d", app_index);
    }
}

// Display information about the bootloader
static void show_bootloader_info(void) {
    ESP_LOGI(TAG, "=== ESP32-P4 Graphical Bootloader Information ===");
    ESP_LOGI(TAG, "Touch-enabled bootloader for ESP32-P4 Function EV Board");
    ESP_LOGI(TAG, "Built with Raylib graphics library");
    ESP_LOGI(TAG, "Supports OTA partition switching");
    ESP_LOGI(TAG, "Select a GUI framework tile to boot the corresponding application");
    ESP_LOGI(TAG, "===================================================");

    // Print partition information
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Currently running partition: %s",
             running_partition->label ? running_partition->label : "unknown");
    ESP_LOGI(TAG, "Partition type: %d, subtype: %d", running_partition->type, running_partition->subtype);
}

// Initialize tiles with their labels and colors
void initialize_tiles(Tile* tiles, int screenWidth, int screenHeight) {
    const char* labels[TILE_COUNT] = {
        "LVGL", "Embedded Wizard", "Slint", "Qt",
        "Candera/CGI Studio", "Raylib", "SDL3", "Info"
    };

    Color colors[TILE_COUNT] = {
        BLUE, GREEN, PURPLE, RED,
        ORANGE, YELLOW, PINK, GRAY
    };

    // Assign OTA indices - first 7 tiles correspond to OTA partitions 0-6, last tile is info (-1)
    int ota_indices[TILE_COUNT] = {0, 1, 2, 3, 4, 5, 6, -1};

    // Calculate grid starting position to center it
    int gridWidth = TILE_COLS * (TILE_WIDTH + TILE_MARGIN) - TILE_MARGIN;
    int gridHeight = TILE_ROWS * (TILE_HEIGHT + TILE_MARGIN) - TILE_MARGIN;
    int startX = (screenWidth - gridWidth) / 2;
    int startY = (screenHeight - gridHeight) / 2;

    for (int i = 0; i < TILE_COUNT; i++) {
        int row = i / TILE_COLS;
        int col = i % TILE_COLS;

        tiles[i].rect.x = startX + col * (TILE_WIDTH + TILE_MARGIN);
        tiles[i].rect.y = startY + row * (TILE_HEIGHT + TILE_MARGIN);
        tiles[i].rect.width = TILE_WIDTH;
        tiles[i].rect.height = TILE_HEIGHT;
        tiles[i].label = labels[i];
        tiles[i].color = colors[i];
        tiles[i].isHovered = false;
        tiles[i].isPressed = false;
        tiles[i].isSelected = false;
        tiles[i].selectionAnimation = 0.0f;
        tiles[i].selectionTime = 0;
        tiles[i].otaIndex = ota_indices[i];
    }
}

// Update tile interaction states with touch support and OTA switching
void update_tiles(Tile* tiles, int count, esp_lcd_touch_handle_t touch_handle) {
    static int64_t last_selection_time = 0;
    Vector2 mousePos = GetMousePosition();
    uint16_t touch_x[1] = {0};
    uint16_t touch_y[1] = {0};
    uint16_t touch_strength[1] = {0};
    uint8_t touch_cnt = 0;
    Vector2 touchPos = {-1, -1};

    // Read touch data if touch handle is available
    if (touch_handle) {
        if (esp_lcd_touch_read_data(touch_handle) == ESP_OK) {
            esp_lcd_touch_get_coordinates(touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 1);
            if (touch_cnt > 0) {
                touchPos.x = touch_x[0];
                touchPos.y = touch_y[0];
            }
        }
    }

    for (int i = 0; i < count; i++) {
        // Reset hover state
        tiles[i].isHovered = false;

        // Check mouse hover
        if (CheckCollisionPointRec(mousePos, tiles[i].rect)) {
            tiles[i].isHovered = true;
        }

        // Check touch hover
        if (touchPos.x >= 0 && touchPos.y >= 0 && CheckCollisionPointRec(touchPos, tiles[i].rect)) {
            tiles[i].isHovered = true;
        }

        // Check for tile selection (touch or mouse)
        bool inputPressed = IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || (touch_cnt > 0);
        bool inputReleased = IsMouseButtonReleased(MOUSE_LEFT_BUTTON) || (touch_cnt == 0 && tiles[i].isPressed);

        if (tiles[i].isHovered && inputPressed) {
            tiles[i].isPressed = true;
            tiles[i].isSelected = true;
            tiles[i].selectionTime = GetTime();
            tiles[i].selectionAnimation = 0.0f;
            ESP_LOGI(TAG, "Tile selected: %s", tiles[i].label);
        } else if (inputReleased) {
            tiles[i].isPressed = false;

            // Handle OTA switching when tile is released (click/touch complete)
            if (tiles[i].isHovered && tiles[i].isSelected) {
                int64_t current_time = esp_timer_get_time();

                // Debounce - prevent multiple rapid selections (500ms debounce)
                if (current_time - last_selection_time > 500000) {
                    last_selection_time = current_time;

                    if (tiles[i].otaIndex >= 0) {
                        // This is an OTA application tile
                        ESP_LOGI(TAG, "Booting to application: %s (OTA index: %d)",
                                tiles[i].label, tiles[i].otaIndex);
                        ota_switch_to_app(tiles[i].otaIndex);
                    } else {
                        // This is the info tile
                        show_bootloader_info();
                    }
                }
            }
        }

        // Update selection animation
        if (tiles[i].isSelected) {
            tiles[i].selectionAnimation += 0.1f;
            if (tiles[i].selectionAnimation > 1.0f) {
                tiles[i].selectionAnimation = 1.0f;
            }
        }
    }
}

// Draw a single tile with selection effects
void draw_tile(const Tile* tile) {
    Color drawColor;

    if (tile->isPressed) {
        // Darken color when pressed
        drawColor = (Color){tile->color.r/2, tile->color.g/2, tile->color.b/2, tile->color.a};
    } else if (tile->isHovered) {
        // Brighten color when hovered
        drawColor = (Color){
            (unsigned char)(fminf(255.0f, tile->color.r + 50)),
            (unsigned char)(fminf(255.0f, tile->color.g + 50)),
            (unsigned char)(fminf(255.0f, tile->color.b + 50)),
            tile->color.a
        };
    } else {
        drawColor = tile->color;
    }

    // Draw tile shadow effect
    if (tile->isSelected) {
        float shadowOffset = 4.0f * (1.0f - tile->selectionAnimation * 0.5f);
        Color shadowColor = (Color){0, 0, 0, 100};
        DrawRectangle(tile->rect.x + shadowOffset, tile->rect.y + shadowOffset,
                     tile->rect.width, tile->rect.height, shadowColor);
    }

    // Draw tile background with rounded corners effect
    DrawRectangleRec(tile->rect, drawColor);

    // Draw selection effects
    if (tile->isSelected) {
        // Pulsing border effect
        float pulse = sinf(GetTime() * 5.0f) * 0.3f + 0.7f;
        int borderWidth = 3 + (int)(tile->selectionAnimation * 5);
        Color borderColor = (Color){
            (unsigned char)(255 * pulse),
            (unsigned char)(215 * pulse),
            (unsigned char)(0 * pulse),  // Gold color
            255
        };
        DrawRectangleLinesEx(tile->rect, borderWidth, borderColor);

        // Selection ring animation
        if (tile->selectionAnimation < 1.0f) {
            float ringSize = tile->selectionAnimation * 30.0f;
            Color ringColor = (Color){
                255,
                215,
                0,
                (unsigned char)((1.0f - tile->selectionAnimation) * 255)
            };
            Vector2 center = {
                tile->rect.x + tile->rect.width / 2,
                tile->rect.y + tile->rect.height / 2
            };
            DrawCircleV(center, ringSize, ringColor);
        }

        // Stars effect for selected tiles
        if (tile->selectionAnimation >= 1.0f) {
            float time = GetTime();
            for (int i = 0; i < 4; i++) {
                float angle = (i * PI / 2) + time * 2.0f;
                float distance = 50.0f + sinf(time * 3.0f + i) * 10.0f;
                Vector2 center = {
                    tile->rect.x + tile->rect.width / 2,
                    tile->rect.y + tile->rect.height / 2
                };
                Vector2 starPos = {
                    center.x + cosf(angle) * distance,
                    center.y + sinf(angle) * distance
                };
                float starSize = 2.0f + sinf(time * 4.0f + i * 1.5f) * 1.0f;

                // Draw star shape
                DrawCircleV(starPos, starSize, (Color){255, 255, 0, 200});
                DrawPoly(starPos, 5, starSize * 1.5f, angle, (Color){255, 255, 0, 150});
            }
        }
    } else {
        // Normal border
        DrawRectangleLinesEx(tile->rect, 3, BLACK);
    }

    // Draw tile label (centered)
    int fontSize = 12;
    if (tile->isSelected) {
        fontSize += (int)(tile->selectionAnimation * 2); // Slightly bigger font when selected
    }
    int textWidth = MeasureText(tile->label, fontSize);
    int textX = tile->rect.x + (tile->rect.width - textWidth) / 2;
    int textY = tile->rect.y + (tile->rect.height - fontSize) / 2;

    Color textColor = WHITE;
    if (tile->isSelected) {
        textColor = (Color){255, 255, 0, 255}; // Yellow text when selected
    }

    DrawText(tile->label, textX, textY, fontSize, textColor);

    // Draw touch indicator when pressed
    if (tile->isPressed) {
        Vector2 center = {
            tile->rect.x + tile->rect.width / 2,
            tile->rect.y + tile->rect.height / 2
        };
        DrawCircleV(center, 8, (Color){255, 255, 255, 150});
    }
}

void raylib_task(void *pvParameter)
{
    // Query actual display dimensions from port
    uint16_t screenWidth = 320;  // Default fallback
    uint16_t screenHeight = 240;

    esp_err_t ret = ray_port_get_dimensions(&screenWidth, &screenHeight);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get display dimensions, using defaults");
    }

    ESP_LOGI(TAG, "Initializing Raylib with display dimensions: %dx%d...", screenWidth, screenHeight);
    InitWindow(screenWidth, screenHeight, "ESP32-P4 GUI Framework Demo");

    ESP_LOGI(TAG, "Raylib Initialized. Entering main loop...");
    ESP_LOGI(TAG, "Screen dimensions: %dx%d", screenWidth, screenHeight);

    // Show bootloader information
    show_bootloader_info();

    // Initialize touch controller
    esp_lcd_touch_handle_t touch_handle = NULL;
    bsp_touch_config_t touch_cfg = {0};
    ret = bsp_touch_new(&touch_cfg, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize touch controller: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Touch controller initialized successfully");
    }

    // Initialize tiles
    Tile tiles[TILE_COUNT];
    initialize_tiles(tiles, screenWidth, screenHeight);

    // Initialize bouncing square variables
    float ballX = screenWidth / 2.0f;
    float ballY = 50.0f;
    float ballSpeedX = 3.0f;
    float ballSpeedY = 2.0f;
    int ballSize = 30;

    // Animation variables for vibrant colors
    int hueShift = 0;

    // Main game loop
    int frameCounter = 0;
    while (!WindowShouldClose()) // Detect window close button or ESC key
    {
        // Update tile interactions with touch support
        update_tiles(tiles, TILE_COUNT, touch_handle);

        // Update bouncing square position
        ballX += ballSpeedX;
        ballY += ballSpeedY;

        // Bounce off walls with some randomness for vibrancy
        if (ballX <= ballSize/2 || ballX >= screenWidth - ballSize/2) {
            ballSpeedX = -ballSpeedX * (0.9f + (rand() % 21) / 100.0f); // Add random factor 0.9-1.1
            ballX = (ballX <= ballSize/2) ? ballSize/2 : screenWidth - ballSize/2;
            hueShift = (hueShift + 30) % 360;
        }

        if (ballY <= ballSize/2 || ballY >= screenHeight - ballSize/2) {
            ballSpeedY = -ballSpeedY * (0.9f + (rand() % 21) / 100.0f); // Add random factor 0.9-1.1
            ballY = (ballY <= ballSize/2) ? ballSize/2 : screenHeight - ballSize/2;
            hueShift = (hueShift + 45) % 360;
        }

        // Add slight gravity effect
        ballSpeedY += 0.1f;

        // Limit maximum speed
        ballSpeedX = fmaxf(-8.0f, fminf(8.0f, ballSpeedX));
        ballSpeedY = fmaxf(-8.0f, fminf(8.0f, ballSpeedY));

        // Begin drawing
        BeginDrawing();

        // Dynamic background color based on animation
        Color bgColor = (Color){
            (unsigned char)(20 + (sinf(frameCounter * 0.01f) * 15 + 15)),
            (unsigned char)(30 + (cosf(frameCounter * 0.015f) * 15 + 15)),
            (unsigned char)(50 + (sinf(frameCounter * 0.02f) * 20 + 20)),
            255
        };
        ClearBackground(bgColor);

        // Draw all tiles
        for (int i = 0; i < TILE_COUNT; i++) {
            draw_tile(&tiles[i]);
        }

        // Draw bouncing square with vibrant colors
        Color ballColor = (Color){
            (unsigned char)(sinf((frameCounter * 0.05f) + hueShift * 0.0174f) * 127 + 128),
            (unsigned char)(sinf((frameCounter * 0.05f + 2.094f) + hueShift * 0.0174f) * 127 + 128),
            (unsigned char)(sinf((frameCounter * 0.05f + 4.189f) + hueShift * 0.0174f) * 127 + 128),
            255
        };

        DrawRectangle(ballX - ballSize/2, ballY - ballSize/2, ballSize, ballSize, ballColor);
        DrawRectangleLinesEx((Rectangle){ballX - ballSize/2, ballY - ballSize/2, ballSize, ballSize}, 2, WHITE);

        // Draw title
        const char *title = "GUI Framework Selector";
        int titleFontSize = 20;
        int titleWidth = MeasureText(title, titleFontSize);
        int titleX = (screenWidth - titleWidth) / 2;
        DrawText(title, titleX, 10, titleFontSize, WHITE);

        // Draw debug info (small text in corner)
        char debugText[64];
        snprintf(debugText, sizeof(debugText), "FPS: %d", GetFPS());
        DrawText(debugText, 5, screenHeight - 40, 10, WHITE);

        // Draw touch indicator
        uint16_t touch_x[1] = {0};
        uint16_t touch_y[1] = {0};
        uint16_t touch_strength[1] = {0};
        uint8_t touch_cnt = 0;
        if (touch_handle && esp_lcd_touch_read_data(touch_handle) == ESP_OK) {
            esp_lcd_touch_get_coordinates(touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 1);
        }

        if (touch_cnt > 0) {
            snprintf(debugText, sizeof(debugText), "Touch: %d at (%d,%d)", touch_cnt, touch_x[0], touch_y[0]);
            DrawText(debugText, 5, screenHeight - 25, 10, GREEN);

            // Draw visual touch indicator
            DrawCircleV((Vector2){touch_x[0], touch_y[0]}, 10, (Color){0, 255, 0, 100});
            DrawCircleV((Vector2){touch_x[0], touch_y[0]}, 5, (Color){0, 255, 0, 200});
        } else {
            DrawText("No touch", 5, screenHeight - 25, 10, GRAY);
        }

        // End drawing
        EndDrawing();
        frameCounter++;
    }

    // De-initialize Raylib (never reached in embedded mode)
    ESP_LOGI(TAG, "De-initializing Raylib...");
    CloseWindow(); // Close window and OpenGL context
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing board display...");
    
    // Initialize display hardware and port layer
    esp_err_t ret = board_init_display();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display: %d", ret);
        return;
    }
    
    ESP_LOGI(TAG, "Creating raylib task with %dKB stack...", RAYLIB_TASK_STACK_SIZE / 1024);
    
    // Create dedicated task for raylib with large stack
    xTaskCreatePinnedToCore(
        raylib_task,              // Task function
        "raylib_task",            // Task name
        RAYLIB_TASK_STACK_SIZE,   // Stack size in bytes
        NULL,                     // Parameters
        5,                        // Priority
        NULL,                     // Task handle
        1                         // Core ID (run on core 1)
    );
}

