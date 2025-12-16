#include "raylib.h"
#include "board_init.h"
#include "esp_raylib_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_lcd_touch.h"
#include "bsp/touch.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "soc/lp_system_reg.h"
#include "cJSON.h"  // For JSON configuration parsing
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"

#define TAG "GraphicalBootloader"
#define RAYLIB_TASK_STACK_SIZE (128 * 1024)  // 128KB stack for software renderer

// RTC register constants for bootloader communication
#define BOOT_REQUEST_RTC_REG     LP_SYSTEM_REG_LP_STORE0_REG
#define BOOT_REQUEST_MAGIC_RTC   0x00544551  // 'BOOT' magic in ASCII

// Configuration constants
#define CONFIG_BASE_PATH "/spiflash"
#define CONFIG_FILE_PATH "/spiflash/config/apps.json"
#define CONFIG_BACKUP_PATH "/spiflash/config/apps.backup.json"
#define CONFIG_DEFAULT_PATH "/spiflash/config/apps.default.json"
#define ICONS_DIR_PATH "/spiflash/icons/"
#define MAX_APPS 16

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

// Icon structure for button graphics
typedef struct {
    char file_path[256];    // Path to icon file
    Rectangle position;     // Position relative to button top-left
    Rectangle size;         // Icon size
    Color fallback_color;   // Color to use if icon fails to load
    Texture2D texture;      // Loaded texture
    bool loaded;            // Whether icon was successfully loaded
} app_icon_t;

// Enhanced app configuration structure
typedef struct {
    char name[64];
    int partition_index;
    struct {
        Color text_color;
        Color bg_color;
        Color hover_color;
        Rectangle position;  // Custom position (optional)
        Rectangle size;      // Custom size (optional)
    } button;
    app_icon_t icon;
    bool enabled;
    bool auto_update;
    char description[256];
} app_config_t;

// Global configuration structure
typedef struct {
    char version[16];
    int tile_cols;
    int tile_rows;
    int tile_margin;
    int tile_width;
    int tile_height;
    int font_size;
    app_config_t apps[MAX_APPS];
    int num_apps;
} bootloader_config_t;

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

// Configuration management globals
static bootloader_config_t g_config;
static bool g_config_loaded = false;
static bool g_spiffs_mounted = false;

// Configuration function prototypes
static esp_err_t init_spiffsfs(void);
static esp_err_t create_directory_structure(void);
static esp_err_t load_configuration(void);
static esp_err_t save_configuration(void) __attribute__((unused));  // Will be used in Phase 4
static esp_err_t create_default_configuration(void);
static esp_err_t parse_color_from_json(cJSON* json, Color* color);
static esp_err_t parse_rectangle_from_json(cJSON* json, Rectangle* rect);
static esp_err_t load_app_icon(app_icon_t* icon);
static void cleanup_configuration(void) __attribute__((unused));  // Will be used in cleanup

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

    // Check if we have a valid app index (0-9 for app list, info handled separately)
    if (app_index < 0 || app_index > 9) {
        ESP_LOGE(TAG, "Invalid app_index %d, must be between 0-9", app_index);
        current_boot_state = BOOT_STATE_ERROR;
        return;
    }

    // Check if this is the Info button (index 9)
    if (app_index == 9) {
        ESP_LOGI(TAG, "Info button pressed - showing system information");
        current_boot_state = BOOT_STATE_ERROR;  // Use error screen to show info
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
        partition_type = 1; // OTA_0 (4.8MB)
    } else if (app_index == 1) {
        partition_type = 2; // OTA_1 (4MB)
    } else if (app_index == 2) {
        partition_type = 3; // OTA_2 (4MB)
    } else if (app_index >= 3 && app_index <= 8) {
        // Demo apps map to available OTA partitions (wrap around)
        partition_type = ((app_index - 3) % 3) + 1;
        ESP_LOGI(TAG, "Demo app %d mapping to OTA partition type %d", app_index - 2, partition_type);
    } else {
        ESP_LOGE(TAG, "App index %d not supported", app_index);
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
    ESP_LOGI(TAG, "Select a button to boot the corresponding application");
    ESP_LOGI(TAG, "===================================================");

    // Print partition information
    const esp_partition_t *factory_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    ESP_LOGI(TAG, "Currently running from factory partition: %s",
             factory_partition->label ? factory_partition->label : "unknown");
    ESP_LOGI(TAG, "Factory partition offset: 0x%x, size: 0x%x", factory_partition->address, factory_partition->size);
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
    static Vector2 last_touch_pos __attribute__((unused)) = {-1, -1};  // Store last touch position for release detection
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
    InitWindow(screenWidth, screenHeight, "Select an application...");

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
            const char *title = "Select an application...";
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
    // Initialize configuration system
    ESP_LOGI(TAG, "Initializing configuration system...");
    esp_err_t config_ret = init_spiffsfs();
    if (config_ret == ESP_OK) {
        config_ret = create_directory_structure();
        if (config_ret == ESP_OK) {
            config_ret = load_configuration();
            if (config_ret == ESP_OK) {
                ESP_LOGI(TAG, "Configuration loaded successfully (%d apps)", g_config.num_apps);
            } else {
                ESP_LOGW(TAG, "Failed to load configuration: %d", config_ret);
                ESP_LOGI(TAG, "Using default configuration");
                create_default_configuration();
            }
        } else {
            ESP_LOGE(TAG, "Failed to create directory structure: %d", config_ret);
        }
    } else {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS: %d", config_ret);
        ESP_LOGI(TAG, "Using embedded default configuration");
        create_default_configuration();
    }

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

// Configuration management functions implementation
static esp_err_t init_spiffsfs(void) {
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_BASE_PATH,
        .partition_label = "bootloader_config",
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%d)", ret);
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("bootloader_config", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%d)", ret);
    } else {
        ESP_LOGI(TAG, "SPIFFS partition size: total: %d, used: %d", total, used);
    }

    g_spiffs_mounted = true;
    return ESP_OK;
}

static esp_err_t create_directory_structure(void) {
    struct stat st;

    // Create config directory
    if (stat("/spiflash/config", &st) != 0) {
        if (mkdir("/spiflash/config", 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create config directory");
            return ESP_FAIL;
        }
    }

    // Create icons directory
    if (stat(ICONS_DIR_PATH, &st) != 0) {
        if (mkdir(ICONS_DIR_PATH, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create icons directory");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Directory structure created");
    return ESP_OK;
}

static esp_err_t parse_color_from_json(cJSON* json, Color* color) {
    if (!json || !color) return ESP_ERR_INVALID_ARG;

    cJSON* r_item = cJSON_GetObjectItem(json, "r");
    cJSON* g_item = cJSON_GetObjectItem(json, "g");
    cJSON* b_item = cJSON_GetObjectItem(json, "b");
    cJSON* a_item = cJSON_GetObjectItem(json, "a");

    if (!cJSON_IsNumber(r_item) || !cJSON_IsNumber(g_item) || !cJSON_IsNumber(b_item)) {
        return ESP_ERR_INVALID_ARG;
    }

    color->r = (uint8_t)cJSON_GetNumberValue(r_item);
    color->g = (uint8_t)cJSON_GetNumberValue(g_item);
    color->b = (uint8_t)cJSON_GetNumberValue(b_item);
    color->a = a_item ? (uint8_t)cJSON_GetNumberValue(a_item) : 255;

    return ESP_OK;
}

static esp_err_t parse_rectangle_from_json(cJSON* json, Rectangle* rect) {
    if (!json || !rect) return ESP_ERR_INVALID_ARG;

    cJSON* x_item = cJSON_GetObjectItem(json, "x");
    cJSON* y_item = cJSON_GetObjectItem(json, "y");
    cJSON* width_item = cJSON_GetObjectItem(json, "width");
    cJSON* height_item = cJSON_GetObjectItem(json, "height");

    if (!cJSON_IsNumber(x_item) || !cJSON_IsNumber(y_item) ||
        !cJSON_IsNumber(width_item) || !cJSON_IsNumber(height_item)) {
        return ESP_ERR_INVALID_ARG;
    }

    rect->x = (float)cJSON_GetNumberValue(x_item);
    rect->y = (float)cJSON_GetNumberValue(y_item);
    rect->width = (float)cJSON_GetNumberValue(width_item);
    rect->height = (float)cJSON_GetNumberValue(height_item);

    return ESP_OK;
}

static esp_err_t load_app_icon(app_icon_t* icon) {
    if (!icon || strlen(icon->file_path) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check if file exists
    struct stat st;
    if (stat(icon->file_path, &st) != 0) {
        ESP_LOGW(TAG, "Icon file not found: %s", icon->file_path);
        return ESP_ERR_NOT_FOUND;
    }

    // Load image
    Image img = LoadImage(icon->file_path);
    if (!img.data) {
        ESP_LOGW(TAG, "Failed to load icon: %s", icon->file_path);
        return ESP_FAIL;
    }

    // Convert to texture
    icon->texture = LoadTextureFromImage(img);
    UnloadImage(img);

    if (!icon->texture.id) {
        ESP_LOGW(TAG, "Failed to create texture for icon: %s", icon->file_path);
        return ESP_FAIL;
    }

    icon->loaded = true;
    ESP_LOGI(TAG, "Successfully loaded icon: %s", icon->file_path);
    return ESP_OK;
}

static esp_err_t create_default_configuration(void) {
    ESP_LOGI(TAG, "Creating default configuration");

    // Initialize with default values
    strcpy(g_config.version, "1.0");
    g_config.tile_cols = TILE_COLS;
    g_config.tile_rows = TILE_ROWS;
    g_config.tile_margin = TILE_MARGIN;
    g_config.tile_width = TILE_WIDTH;
    g_config.tile_height = TILE_HEIGHT;
    g_config.font_size = 16;
    g_config.num_apps = 10;  // 3 OTA + 6 demo + 1 info

    // Create default app configurations
    const char* default_names[] = {
        "OTA App 1", "OTA App 2", "OTA App 3",
        "Demo 1", "Demo 2", "Demo 3", "Demo 4", "Demo 5", "Demo 6", "Info"
    };
    Color default_colors[] = {
        SKYBLUE, LIME, VIOLET,
        BLUE, GREEN, PURPLE, RED, ORANGE, YELLOW, GRAY
    };

    for (int i = 0; i < 10; i++) {
        app_config_t* app = &g_config.apps[i];
        strcpy(app->name, default_names[i]);

        // Map partition indices
        if (i < 3) {
            app->partition_index = i;  // OTA_0, OTA_1, OTA_2
        } else {
            app->partition_index = i - 3;  // Demo apps map to OTA partitions when available
        }

        app->enabled = true;
        app->auto_update = false;

        if (i < 3) {
            snprintf(app->description, sizeof(app->description), "OTA application %d - 4.8MB/4MB partition", i + 1);
        } else if (i < 9) {
            snprintf(app->description, sizeof(app->description), "Demo application %d", i - 2);
        } else {
            strcpy(app->description, "System information and partition details");
        }

        // Default button styling
        app->button.text_color = WHITE;
        app->button.bg_color = default_colors[i];
        app->button.hover_color = ColorBrightness(default_colors[i], 0.2f);

        // Default icon configuration
        strcpy(app->icon.file_path, "");
        app->icon.position = (Rectangle){10, 5, 32, 32};
        app->icon.size = (Rectangle){32, 32, 0, 0};
        app->icon.fallback_color = default_colors[i];
        app->icon.loaded = false;
    }

    // Handle Info button specifically
    app_config_t* info_app = &g_config.apps[9];
    strcpy(info_app->name, "System Info");
    info_app->partition_index = -1;
    strcpy(info_app->description, "Partition information and system details");
    info_app->icon.fallback_color = GRAY;

    g_config_loaded = true;
    return ESP_OK;
}

static void cleanup_configuration(void) {
    // Unload all loaded textures
    for (int i = 0; i < g_config.num_apps; i++) {
        if (g_config.apps[i].icon.loaded) {
            UnloadTexture(g_config.apps[i].icon.texture);
            g_config.apps[i].icon.loaded = false;
        }
    }
}

static esp_err_t load_configuration(void) {
    FILE* file = fopen(CONFIG_FILE_PATH, "r");
    if (!file) {
        ESP_LOGW(TAG, "Configuration file not found: %s", CONFIG_FILE_PATH);
        return ESP_ERR_NOT_FOUND;
    }

    // Read file content
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 65536) {  // Max 64KB
        fclose(file);
        ESP_LOGE(TAG, "Invalid configuration file size: %ld", file_size);
        return ESP_ERR_INVALID_SIZE;
    }

    char* buffer = malloc(file_size + 1);
    if (!buffer) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read_size = fread(buffer, 1, file_size, file);
    fclose(file);

    if (read_size != (size_t)file_size) {
        free(buffer);
        return ESP_ERR_INVALID_SIZE;
    }
    buffer[file_size] = '\0';

    // Parse JSON
    cJSON* root = cJSON_Parse(buffer);
    free(buffer);

    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON configuration");
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t ret = ESP_OK;

    // Parse version
    cJSON* version = cJSON_GetObjectItem(root, "version");
    if (cJSON_IsString(version)) {
        strncpy(g_config.version, cJSON_GetStringValue(version), sizeof(g_config.version) - 1);
    } else {
        strcpy(g_config.version, "1.0");
    }

    // Parse layout
    cJSON* layout = cJSON_GetObjectItem(root, "layout");
    if (layout) {
        cJSON* item = cJSON_GetObjectItem(layout, "tile_cols");
        if (cJSON_IsNumber(item)) g_config.tile_cols = cJSON_GetNumberValue(item);

        item = cJSON_GetObjectItem(layout, "tile_rows");
        if (cJSON_IsNumber(item)) g_config.tile_rows = cJSON_GetNumberValue(item);

        item = cJSON_GetObjectItem(layout, "tile_margin");
        if (cJSON_IsNumber(item)) g_config.tile_margin = cJSON_GetNumberValue(item);

        item = cJSON_GetObjectItem(layout, "tile_width");
        if (cJSON_IsNumber(item)) g_config.tile_width = cJSON_GetNumberValue(item);

        item = cJSON_GetObjectItem(layout, "tile_height");
        if (cJSON_IsNumber(item)) g_config.tile_height = cJSON_GetNumberValue(item);
    }

    // Parse apps
    cJSON* apps = cJSON_GetObjectItem(root, "apps");
    if (cJSON_IsArray(apps)) {
        int app_count = cJSON_GetArraySize(apps);
        g_config.num_apps = (app_count > MAX_APPS) ? MAX_APPS : app_count;

        for (int i = 0; i < g_config.num_apps; i++) {
            cJSON* app_obj = cJSON_GetArrayItem(apps, i);
            if (!app_obj) continue;

            app_config_t* app = &g_config.apps[i];

            // Parse name
            cJSON* item = cJSON_GetObjectItem(app_obj, "name");
            if (cJSON_IsString(item)) {
                strncpy(app->name, cJSON_GetStringValue(item), sizeof(app->name) - 1);
            }

            // Parse partition index
            item = cJSON_GetObjectItem(app_obj, "partition_index");
            if (cJSON_IsNumber(item)) {
                app->partition_index = cJSON_GetNumberValue(item);
            }

            // Parse button properties
            cJSON* button = cJSON_GetObjectItem(app_obj, "button");
            if (button) {
                item = cJSON_GetObjectItem(button, "text_color");
                if (cJSON_IsObject(item)) {
                    parse_color_from_json(item, &app->button.text_color);
                } else if (cJSON_IsString(item)) {
                    // Parse hex color string like "#RRGGBB" or "#RRGGBBAA"
                    const char* hex_str = cJSON_GetStringValue(item);
                    if (hex_str && strlen(hex_str) >= 7 && hex_str[0] == '#') {
                        unsigned int color;
                        sscanf(hex_str, "#%x", &color);
                        app->button.text_color = (Color){(color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, 255};
                    }
                }

                item = cJSON_GetObjectItem(button, "bg_color");
                if (cJSON_IsObject(item)) {
                    parse_color_from_json(item, &app->button.bg_color);
                } else if (cJSON_IsString(item)) {
                    const char* hex_str = cJSON_GetStringValue(item);
                    if (hex_str && strlen(hex_str) >= 7 && hex_str[0] == '#') {
                        unsigned int color;
                        sscanf(hex_str, "#%x", &color);
                        app->button.bg_color = (Color){(color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, 255};
                    }
                }

                item = cJSON_GetObjectItem(button, "hover_color");
                if (cJSON_IsObject(item)) {
                    parse_color_from_json(item, &app->button.hover_color);
                }

                cJSON* pos = cJSON_GetObjectItem(button, "position");
                if (pos) {
                    parse_rectangle_from_json(pos, &app->button.position);
                }

                cJSON* size = cJSON_GetObjectItem(button, "size");
                if (size) {
                    parse_rectangle_from_json(size, &app->button.size);
                }
            }

            // Parse icon properties
            cJSON* icon = cJSON_GetObjectItem(app_obj, "icon");
            if (icon) {
                item = cJSON_GetObjectItem(icon, "file");
                if (cJSON_IsString(item)) {
                    strncpy(app->icon.file_path, cJSON_GetStringValue(item), sizeof(app->icon.file_path) - 1);
                }

                cJSON* pos = cJSON_GetObjectItem(icon, "position");
                if (pos) {
                    parse_rectangle_from_json(pos, &app->icon.position);
                }

                cJSON* size = cJSON_GetObjectItem(icon, "size");
                if (size) {
                    parse_rectangle_from_json(size, &app->icon.size);
                }

                item = cJSON_GetObjectItem(icon, "fallback_color");
                if (cJSON_IsObject(item)) {
                    parse_color_from_json(item, &app->icon.fallback_color);
                }

                // Try to load the icon
                load_app_icon(&app->icon);
            }

            // Parse other properties
            item = cJSON_GetObjectItem(app_obj, "enabled");
            if (cJSON_IsBool(item)) {
                app->enabled = cJSON_IsTrue(item);
            }

            item = cJSON_GetObjectItem(app_obj, "auto_update");
            if (cJSON_IsBool(item)) {
                app->auto_update = cJSON_IsTrue(item);
            }

            item = cJSON_GetObjectItem(app_obj, "description");
            if (cJSON_IsString(item)) {
                strncpy(app->description, cJSON_GetStringValue(item), sizeof(app->description) - 1);
            }
        }
    } else {
        ret = ESP_ERR_INVALID_RESPONSE;
    }

    cJSON_Delete(root);
    if (ret == ESP_OK) {
        g_config_loaded = true;
        ESP_LOGI(TAG, "Configuration loaded successfully");
    }

    return ret;
}

static esp_err_t save_configuration(void) {
    cJSON* root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    // Add version
    cJSON_AddStringToObject(root, "version", g_config.version);

    // Add layout
    cJSON* layout = cJSON_CreateObject();
    cJSON_AddNumberToObject(layout, "tile_cols", g_config.tile_cols);
    cJSON_AddNumberToObject(layout, "tile_rows", g_config.tile_rows);
    cJSON_AddNumberToObject(layout, "tile_margin", g_config.tile_margin);
    cJSON_AddNumberToObject(layout, "tile_width", g_config.tile_width);
    cJSON_AddNumberToObject(layout, "tile_height", g_config.tile_height);
    cJSON_AddNumberToObject(layout, "font_size", g_config.font_size);
    cJSON_AddItemToObject(root, "layout", layout);

    // Add apps
    cJSON* apps = cJSON_CreateArray();
    for (int i = 0; i < g_config.num_apps; i++) {
        app_config_t* app = &g_config.apps[i];
        cJSON* app_obj = cJSON_CreateObject();

        cJSON_AddStringToObject(app_obj, "name", app->name);
        cJSON_AddNumberToObject(app_obj, "partition_index", app->partition_index);
        cJSON_AddBoolToObject(app_obj, "enabled", app->enabled);
        cJSON_AddBoolToObject(app_obj, "auto_update", app->auto_update);
        cJSON_AddStringToObject(app_obj, "description", app->description);

        // Add button configuration
        cJSON* button = cJSON_CreateObject();
        cJSON_AddItemToObject(app_obj, "button", button);

        // Add icon configuration
        cJSON* icon = cJSON_CreateObject();
        cJSON_AddStringToObject(icon, "file", app->icon.file_path);
        cJSON_AddNumberToObject(icon, "fallback_color", app->icon.fallback_color.r << 16 | app->icon.fallback_color.g << 8 | app->icon.fallback_color.b);
        cJSON_AddItemToObject(app_obj, "icon", icon);

        cJSON_AddItemToArray(apps, app_obj);
    }
    cJSON_AddItemToObject(root, "apps", apps);

    // Convert to string
    char* json_string = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_string) {
        return ESP_ERR_NO_MEM;
    }

    // Create backup first
    FILE* backup_file = fopen(CONFIG_BACKUP_PATH, "w");
    if (backup_file) {
        fputs(json_string, backup_file);
        fclose(backup_file);
    }

    // Write to main config file
    FILE* file = fopen(CONFIG_FILE_PATH, "w");
    if (!file) {
        free(json_string);
        return ESP_ERR_NOT_FOUND;
    }

    size_t written = fwrite(json_string, 1, strlen(json_string), file);
    fclose(file);
    free(json_string);

    if (written != strlen(json_string)) {
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Configuration saved successfully");
    return ESP_OK;
}

