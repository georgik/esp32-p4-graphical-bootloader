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
#include "bootloader_api.h"
#include "soc/lp_system_reg.h"

#define TAG "RaylibDemo"
#define RAYLIB_TASK_STACK_SIZE (128 * 1024)  // 128KB stack for software renderer

// RTC register constants for bootloader communication
#define BOOT_REQUEST_RTC_REG     LP_SYSTEM_REG_LP_STORE0_REG
#define BOOT_REQUEST_MAGIC_RTC   0x00544551  // 'BOOT' magic in ASCII

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

// Boot state for visual feedback
typedef enum {
    BOOT_STATE_SELECTING,
    BOOT_STATE_BOOTING,
    BOOT_STATE_ERROR
} boot_state_t;

static boot_state_t current_boot_state = BOOT_STATE_SELECTING;
static const char* booting_app_name = NULL;
static int booting_animation_time = 0;
static int selected_tile_index = -1;

// Get app label by OTA index
static const char* get_app_label_by_index(int app_index) {
    const char* labels[] = {
        "LVGL", "Embedded Wizard", "Slint", "Qt",
        "Candera/CGI Studio", "Raylib", "SDL3", "Info"
    };
    if (app_index >= 0 && app_index < 8) {
        return labels[app_index];
    }
    return "Unknown";
}

// Boot switching function using RTC register for bootloader communication
static void ota_switch_to_app(int app_index) {
    ESP_LOGI(TAG, "Attempting to switch to app partition %d", app_index);

    // Check if we have a valid app index (0-1 for OTA partitions, 2 for factory, 7 is info)
    if (app_index < 0 || app_index > 7) {
        ESP_LOGE(TAG, "Invalid app_index %d, must be between 0-7", app_index);
        current_boot_state = BOOT_STATE_ERROR;
        return;
    }

    // Set booting state for visual feedback
    current_boot_state = BOOT_STATE_BOOTING;
    booting_app_name = get_app_label_by_index(app_index);
    booting_animation_time = 0;

    ESP_LOGI(TAG, "Preparing to boot application: %s (index: %d)", booting_app_name, app_index);

    // Map app_index to partition type for bootloader
    uint32_t partition_type;
    if (app_index == 0) {
        partition_type = 1; // OTA_0
    } else if (app_index == 1) {
        partition_type = 2; // OTA_1
    } else if (app_index == 2) {
        partition_type = 0; // Factory
    } else {
        ESP_LOGE(TAG, "App index %d not supported yet", app_index);
        current_boot_state = BOOT_STATE_ERROR;
        return;
    }

    ESP_LOGI(TAG, "Writing boot request to RTC register: magic=0x%08x, partition_type=%d",
             BOOT_REQUEST_MAGIC_RTC, partition_type);

    // Write boot request to RTC register for bootloader to read
    // Combine magic and partition type: lower 24 bits = magic, upper 8 bits = partition type
    uint32_t rtc_value = BOOT_REQUEST_MAGIC_RTC | (partition_type << 24);
    REG_WRITE(BOOT_REQUEST_RTC_REG, rtc_value);

    ESP_LOGI(TAG, "RTC register updated successfully, value: 0x%08x", rtc_value);

    // Add delay to show booting animation
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Restarting now for bootloader to handle the boot request...");
    esp_restart();
}

// Draw booting screen with animation
static void draw_booting_screen(int screenWidth, int screenHeight, int frameCounter) {
    // Dark background during booting
    Color bgColor = (Color){20, 20, 30, 255};
    ClearBackground(bgColor);

    // Pulsing boot animation
    float pulse = sinf(frameCounter * 0.05f) * 0.3f + 0.7f;

    // Main booting message
    const char* mainMessage = "Booting Application...";
    int mainFontSize = 30;
    int mainWidth = MeasureText(mainMessage, mainFontSize);
    int mainX = (screenWidth - mainWidth) / 2;
    int mainY = screenHeight / 2 - 60;

    Color mainColor = (Color){
        (unsigned char)(255 * pulse),
        (unsigned char)(255 * pulse),
        (unsigned char)(255 * pulse),
        255
    };
    DrawText(mainMessage, mainX, mainY, mainFontSize, mainColor);

    // Application name
    if (booting_app_name) {
        char appMessage[64];
        snprintf(appMessage, sizeof(appMessage), "Starting %s...", booting_app_name);
        int appFontSize = 20;
        int appWidth = MeasureText(appMessage, appFontSize);
        int appX = (screenWidth - appWidth) / 2;
        int appY = screenHeight / 2 - 20;

        DrawText(appMessage, appX, appY, appFontSize, YELLOW);
    }

    // Loading dots animation
    int dotCount = (frameCounter / 30) % 4;
    for (int i = 0; i < dotCount; i++) {
        int dotX = screenWidth / 2 - 30 + i * 20;
        int dotY = screenHeight / 2 + 20;
        int dotSize = 8 + (int)(sinf(frameCounter * 0.1f + i) * 3);

        DrawCircleV((Vector2){dotX, dotY}, dotSize,
                   (Color){255, 215, 0, (unsigned char)(255 * pulse)});
    }

    // Progress bar
    int barWidth = 300;
    int barHeight = 10;
    int barX = (screenWidth - barWidth) / 2;
    int barY = screenHeight / 2 + 50;

    // Background bar
    DrawRectangle(barX, barY, barWidth, barHeight, (Color){50, 50, 60, 255});

    // Animated progress
    int progress = (frameCounter * 2) % (barWidth + 40);
    if (progress > barWidth) {
        progress = barWidth - (progress - barWidth);
    }
    DrawRectangle(barX, barY, progress, barHeight, (Color){255, 215, 0, 255});

    // Corner message
    const char* cornerMsg = "Please wait...";
    int cornerFontSize = 12;
    DrawText(cornerMsg, 5, screenHeight - 20, cornerFontSize, GRAY);
}

// Draw error screen with restart button
static bool draw_error_screen(int screenWidth, int screenHeight, esp_lcd_touch_handle_t touch_handle) {
    Color bgColor = (Color){40, 20, 20, 255};
    ClearBackground(bgColor);

    const char* errorMsg = "Boot Failed!";
    int errorFontSize = 30;
    int errorWidth = MeasureText(errorMsg, errorFontSize);
    int errorX = (screenWidth - errorWidth) / 2;
    int errorY = screenHeight / 2 - 60;

    DrawText(errorMsg, errorX, errorY, errorFontSize, RED);

    const char* retryMsg = "Please try again";
    int retryFontSize = 16;
    int retryWidth = MeasureText(retryMsg, retryFontSize);
    int retryX = (screenWidth - retryWidth) / 2;
    int retryY = screenHeight / 2 - 20;

    DrawText(retryMsg, retryX, retryY, retryFontSize, WHITE);

    // Restart button
    const char* restartText = "RESTART";
    int restartFontSize = 20;
    int restartWidth = MeasureText(restartText, restartFontSize);
    int buttonWidth = restartWidth + 40;
    int buttonHeight = 50;
    int buttonX = (screenWidth - buttonWidth) / 2;
    int buttonY = screenHeight / 2 + 20;

    Rectangle restartButton = {buttonX, buttonY, buttonWidth, buttonHeight};

    // Check if button is pressed
    bool buttonPressed = false;
    Vector2 mousePos = GetMousePosition();

    // Read touch data
    uint16_t touch_x[1] = {0};
    uint16_t touch_y[1] = {0};
    uint16_t touch_strength[1] = {0};
    uint8_t touch_cnt = 0;

    if (touch_handle && esp_lcd_touch_read_data(touch_handle) == ESP_OK) {
        esp_lcd_touch_get_coordinates(touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 1);
    }

    // Check if mouse or touch is over button
    bool mouseOverButton = CheckCollisionPointRec(mousePos, restartButton);
    bool touchOverButton = (touch_cnt > 0 && CheckCollisionPointRec((Vector2){touch_x[0], touch_y[0]}, restartButton));

    if (mouseOverButton || touchOverButton) {
        DrawRectangleRec(restartButton, (Color){255, 100, 100, 255});
        DrawRectangleLinesEx(restartButton, 3, (Color){255, 255, 255, 255});
        buttonPressed = IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || (touch_cnt > 0);
    } else {
        DrawRectangleRec(restartButton, (Color){180, 50, 50, 255});
        DrawRectangleLinesEx(restartButton, 3, (Color){200, 200, 200, 255});
    }

    // Draw restart button text
    int textX = restartButton.x + (restartButton.width - restartWidth) / 2;
    int textY = restartButton.y + (restartButton.height - restartFontSize) / 2;
    DrawText(restartText, textX, textY, restartFontSize, WHITE);

    // Check if button was released (trigger restart)
    static bool wasButtonPressed = false;
    bool mouseReleased = IsMouseButtonReleased(MOUSE_LEFT_BUTTON);
    bool touchReleased = (touch_cnt == 0 && wasButtonPressed);
    bool buttonReleased = mouseReleased || touchReleased;

    if (buttonPressed) {
        wasButtonPressed = true;
    } else {
        wasButtonPressed = false;
    }

    return buttonReleased && (mouseOverButton || touchOverButton);
}

// Display information about the bootloader
static void show_bootloader_info(void) {
    ESP_LOGI(TAG, "=== ESP32-P4 Graphical Bootloader Information ===");
    ESP_LOGI(TAG, "Touch-enabled bootloader for ESP32-P4 Function EV Board");
    ESP_LOGI(TAG, "Built with Raylib graphics library");
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
        "Demo 1", "Demo 2", "Demo 3", "Demo 4",
        "Demo 5", "Demo 6", "Demo 7", "Info"
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
    static bool was_touching = false;
    static Vector2 last_touch_pos = {-1, -1};  // Store last touch position for release detection
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
                // Store current touch position when touching
                last_touch_pos = touchPos;
            }
        }
    }

    bool is_touching = touch_cnt > 0;

    for (int i = 0; i < count; i++) {
        // Reset hover state
        tiles[i].isHovered = false;

        // Check mouse hover
        if (CheckCollisionPointRec(mousePos, tiles[i].rect)) {
            tiles[i].isHovered = true;
        }

        // Check touch hover
        if (is_touching && CheckCollisionPointRec(touchPos, tiles[i].rect)) {
            tiles[i].isHovered = true;
        }

        // Check for tile selection (touch or mouse)
        bool inputPressed = IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || is_touching;

        // Detect touch release: was touching but now not touching
        bool touch_released = was_touching && !is_touching;
        bool inputReleased = IsMouseButtonReleased(MOUSE_LEFT_BUTTON) || touch_released;

        if (tiles[i].isHovered && inputPressed) {
            tiles[i].isPressed = true;
            tiles[i].isSelected = true;
            tiles[i].selectionTime = GetTime();
            tiles[i].selectionAnimation = 0.0f;
            selected_tile_index = i;  // Store which tile is being pressed
            ESP_LOGI(TAG, "Tile selected: %s (touch: %s, mouse: %s)",
                    tiles[i].label, is_touching ? "yes" : "no",
                    IsMouseButtonPressed(MOUSE_LEFT_BUTTON) ? "yes" : "no");
        } else if (inputReleased) {
            if (tiles[i].isPressed) {
                ESP_LOGI(TAG, "Tile released: %s", tiles[i].label);
            }
            tiles[i].isPressed = false;

            // Handle OTA switching when tile is released (click/touch complete)
            // For touch: use the stored pressed tile index and last touch position
            // For mouse: use current hover state
            bool should_trigger_ota = false;

            if (touch_released && selected_tile_index == i) {
                // Touch release on the same tile that was pressed
                should_trigger_ota = tiles[i].isSelected;
                ESP_LOGI(TAG, "Touch release on tile %d - should_trigger_ota: %s", i, should_trigger_ota ? "true" : "false");
            } else if (!touch_released && tiles[i].isHovered && tiles[i].isSelected) {
                // Mouse release on hovered tile
                should_trigger_ota = true;
            }

            ESP_LOGI(TAG, "Checking OTA switch - tile: %d, isHovered: %s, isSelected: %s, should_trigger_ota: %s, otaIndex: %d",
                    i, tiles[i].isHovered ? "true" : "false", tiles[i].isSelected ? "true" : "false",
                    should_trigger_ota ? "true" : "false", tiles[i].otaIndex);

            if (should_trigger_ota) {
                int64_t current_time = esp_timer_get_time();
                ESP_LOGI(TAG, "Release conditions met - current_time: %lld, last_selection_time: %lld",
                        current_time, last_selection_time);

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
                } else {
                    ESP_LOGI(TAG, "Debounce blocked - time since last: %lld us",
                            current_time - last_selection_time);
                }
            }
        }

        // Clear pressed tile index if this tile was released
        if (inputReleased && selected_tile_index == i) {
            selected_tile_index = -1;
        }

        // Update selection animation
        if (tiles[i].isSelected) {
            tiles[i].selectionAnimation += 0.1f;
            if (tiles[i].selectionAnimation > 1.0f) {
                tiles[i].selectionAnimation = 1.0f;
            }
        }
    }

    // Update touch state for next frame
    was_touching = is_touching;
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

        // State-based rendering
        if (current_boot_state == BOOT_STATE_BOOTING) {
            // Draw booting screen with animation
            draw_booting_screen(screenWidth, screenHeight, booting_animation_time);
            booting_animation_time++;
        } else if (current_boot_state == BOOT_STATE_ERROR) {
            // Draw error screen with restart button
            bool restart_requested = draw_error_screen(screenWidth, screenHeight, touch_handle);
            if (restart_requested) {
                ESP_LOGI(TAG, "Restart requested by user - resetting to selection mode");
                current_boot_state = BOOT_STATE_SELECTING;
                selected_tile_index = -1;
                booting_animation_time = 0;

                // Clear any pending touch inputs
                if (touch_handle) {
                    esp_lcd_touch_read_data(touch_handle);
                }
            }
        } else {
            // Draw normal selection interface
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
            //snprintf(debugText, sizeof(debugText), "FPS: %d", GetFPS());
            //DrawText(debugText, 5, screenHeight - 40, 10, WHITE);

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

