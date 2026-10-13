// Tutorial : https://youtu.be/jYcxUgxz9ks
// Use board "ESP32 Dev Module" (last tested on v3.2.0)

#include <Arduino_GFX_Library.h> // Install "GFX Library for Arduino" with the Library Manager (last tested on v1.6.0)
                                 // Install "JPEGDEC" with the Library Manager (last tested on v1.8.2)
#include <JPEGDEC.h>             // For JPEGDRAW struct definition
#include "MjpegClass.h"          // Included in this project
#include "SD.h"                  // Included with the Espressif Arduino Core (last tested on v3.2.0)

// Forward declarations for functions used before definition
void runSdDiagnostics();
bool loadMjpegFilesList();
void showCenteredMessage(const char *const lines[], uint8_t lineCount);
String buildMjpegPath(const String &fileName);
bool isMjpegVideoFileName(const String &fileName);
void printFileReadTest(fs::File &file);
String formatBytes(size_t bytes);
void mjpegPlayFromSDCard(char *mjpegFilename);
void playSelectedMjpeg(int mjpegIndex);
int jpegDrawCallback(JPEGDRAW *pDraw);
const char *sdCardTypeName(uint8_t cardType);

// Pins for the display
#define BL_PIN 21 // On some cheap yellow display model, BL pin is 27
#define SD_CS 5
#define SD_MISO 19
#define SD_MOSI 23
#define SD_SCK 18

#define BOOT_PIN 0                   // Boot pin
#define BOOT_BUTTON_DEBOUCE_TIME 400 // Debounce time when reading the boot button in milliseconds

// Some model of cheap Yellow display works only at 40Mhz
// #define DISPLAY_SPI_SPEED 40000000L // 40MHz 
#define DISPLAY_SPI_SPEED 80000000L // 80MHz 


#define SD_SPI_SPEED 20000000L      // 20Mhz - more reliable for SD cards on CYD
#define ENABLE_SD_DIAGNOSTICS true  // Enable diagnostics to help troubleshoot SD issues

// SD.open() adds the /sd mount point internally; paths here start at the card root.
const char *MJPEG_FOLDER = "/mjpeg";

// Storage for files to read on the SD card
#define MAX_FILES 20 // Maximum number of files, adjust as needed
String mjpegFileList[MAX_FILES];
uint32_t mjpegFileSizes[MAX_FILES] = {0}; // Store each GIF file's size in bytes
int mjpegCount = 0;
static int currentMjpegIndex = 0;
static bool playbackReady = false;
static fs::File mjpegFile; // temp gif file holder

// Global variables for mjpeg
MjpegClass mjpeg;
int total_frames;
unsigned long total_read_video;
unsigned long total_decode_video;
unsigned long total_show_video;
unsigned long start_ms, curr_ms;
long output_buf_size, estimateBufferSize;
uint8_t *mjpeg_buf;
uint16_t *output_buf;

// Separate SPIClass objects must also use separate hardware hosts on this board.
SPIClass display_spi(HSPI);
Arduino_DataBus *bus = new Arduino_HWSPI(2 /* DC */, 15 /* CS */, 14 /* SCK */, 13 /* MOSI */, 12 /* MISO */, &display_spi);
Arduino_GFX *gfx = new Arduino_ILI9341(bus);

// SD Card reader is on a separate SPI
SPIClass sd_spi(VSPI);

// Interrupt to skip to the next mjpeg when the boot button is pressed
volatile bool skipRequested = false; // set in ISR, read in loop()
volatile uint32_t isrTick = 0;       // tick count captured in ISR
uint32_t lastPress = 0;              // used in main context for debounc

void IRAM_ATTR onButtonPress()
{
    skipRequested = true;                 // flag handled in the playback loop
    isrTick = xTaskGetTickCountFromISR(); // safe, 1-tick resolution
}

void setup()
{
    Serial.begin(115200);
    delay(100);
    Serial.println("\n\n=== ESP32-2432S028 Video Player Starting ===");

    // Set display backlight to High
    pinMode(BL_PIN, OUTPUT);
    digitalWrite(BL_PIN, HIGH);

    // Display initialization
    Serial.println("Display initialization");
    if (!gfx->begin(DISPLAY_SPI_SPEED))
    {
        Serial.println("Display initialization failed!");
        while (true)
        {
            /* no need to continue */
        }
    }
    gfx->setRotation(0);
    gfx->fillScreen(RGB565_BLACK);
    // gfx->invertDisplay(true); // on some cheap yellow models, display must be inverted
    Serial.printf("Screen size Width=%d,Height=%d\n", gfx->width(), gfx->height());

    // SD card initialization
    Serial.println("SD Card initialization (display=HSPI, SD=VSPI)");
    sd_spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, sd_spi, SD_SPI_SPEED, "/sd") || SD.cardType() == CARD_NONE)
    {
        Serial.println("ERROR: SD card not found or file system mount failed!");
        const char *lines[] = {"SD card not found", "Insert/check card", "and reset device."};
        showCenteredMessage(lines, sizeof(lines) / sizeof(lines[0]));
        return;
    }
    Serial.printf("SD Card mounted at /sd; video folder on card: %s\n", MJPEG_FOLDER);
    runSdDiagnostics();

    if (!loadMjpegFilesList())
    {
        const char *lines[] = {
            "No playable files",
            "",
            "Verify .mjpeg files",
            "in the SD card's",
            MJPEG_FOLDER,
            "folder, then reset."};
        showCenteredMessage(lines, sizeof(lines) / sizeof(lines[0]));
        return;
    }

    // Buffer allocation for mjpeg playing
    Serial.println("Buffer allocation");
    output_buf_size = gfx->width() * 4 * 2;
    output_buf = (uint16_t *)heap_caps_aligned_alloc(16, output_buf_size * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!output_buf)
    {
        Serial.println("output_buf aligned_alloc failed!");
        while (true)
        {
            /* no need to continue */
        }
    }
    estimateBufferSize = gfx->width() * gfx->height() * 2 / 5;
    mjpeg_buf = (uint8_t *)heap_caps_malloc(estimateBufferSize, MALLOC_CAP_8BIT);
    if (!mjpeg_buf)
    {
        Serial.println("mjpeg_buf allocation failed!");
        while (true)
        {
            /* no need to continue */
        }
    }

    // Set the boot button to skip the current mjpeg playing and go to the next
    pinMode(BOOT_PIN, INPUT);                        
    attachInterrupt(digitalPinToInterrupt(BOOT_PIN), // fast ISR
                    onButtonPress, FALLING);         // press == LOW
    playbackReady = true;
}

void loop()
{
    if (!playbackReady)
    {
        delay(1000);
        return;
    }
    playSelectedMjpeg(currentMjpegIndex);
    currentMjpegIndex++;
    if (currentMjpegIndex >= mjpegCount)
    {
        currentMjpegIndex = 0;
    }
}

void showCenteredMessage(const char *const lines[], uint8_t lineCount)
{
    const uint8_t textSize = 2;
    const int16_t lineSpacing = 8 * textSize + 4;
    int16_t y = (gfx->height() - (lineCount - 1) * lineSpacing) / 2;

    gfx->fillScreen(RGB565_BLACK);
    gfx->setFont();
    gfx->setTextSize(textSize);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextWrap(false);
    for (uint8_t i = 0; i < lineCount; i++)
    {
        int16_t x1, y1;
        uint16_t w, h;
        gfx->getTextBounds(lines[i], 0, 0, &x1, &y1, &w, &h);
        gfx->setCursor(
            gfx->width() / 2 - w / 2 - x1,
            y - h / 2 - y1);
        gfx->print(lines[i]);
        y += lineSpacing;
    }
}

// Play the current mjpeg
void playSelectedMjpeg(int mjpegIndex)
{
    // Build the full path for the selected mjpeg
    String fullPath = buildMjpegPath(mjpegFileList[mjpegIndex]);
    char mjpegFilename[128];
    fullPath.toCharArray(mjpegFilename, sizeof(mjpegFilename));

    Serial.printf("Playing %s\n", mjpegFilename);
    mjpegPlayFromSDCard(mjpegFilename);
}

// Callback function to draw a JPEG
int jpegDrawCallback(JPEGDRAW *pDraw)
{
    unsigned long s = millis();
    gfx->draw16bitBeRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
    total_show_video += millis() - s;
    return 1;
}

// Play a mjpeg stored on the SD card
void mjpegPlayFromSDCard(char *mjpegFilename)
{
    Serial.printf("Opening %s\n", mjpegFilename);
    
    File mjpegFile = SD.open(mjpegFilename, FILE_READ);

    if (!mjpegFile || mjpegFile.isDirectory())
    {
        Serial.printf("ERROR: Failed to open %s file for reading\n", mjpegFilename);
        Serial.printf("  File object valid: %d, isDirectory: %d\n", (bool)mjpegFile, mjpegFile.isDirectory());
        return;
    }
    
    Serial.printf("File opened successfully, size: %lu bytes\n", mjpegFile.size());
    Serial.println("MJPEG start");
    gfx->fillScreen(RGB565_BLACK);

    start_ms = millis();
    curr_ms = millis();
    total_frames = 0;
    total_read_video = 0;
    total_decode_video = 0;
    total_show_video = 0;

    if (!mjpeg.setup(
        &mjpegFile, mjpeg_buf, jpegDrawCallback, true /* useBigEndian */,
        0 /* x */, 0 /* y */, gfx->width() /* widthLimit */, gfx->height() /* heightLimit */))
    {
        Serial.printf("ERROR: MJPEG reader initialization failed for %s\n", mjpegFilename);
        mjpegFile.close();
        return;
    }

    while (!skipRequested && mjpegFile.available() && mjpeg.readMjpegBuf())
    {
        // Read video
        total_read_video += millis() - curr_ms;
        curr_ms = millis();

        // Play video
        mjpeg.drawJpg();
        total_decode_video += millis() - curr_ms;

        curr_ms = millis();
        total_frames++;
    }
    /* We exited because the button was pressed or the video ended */
    if (skipRequested) // pressed?
    {
        uint32_t now = millis(); // safe here
        if (now - lastPress < BOOT_BUTTON_DEBOUCE_TIME)
        {
            // ignore if it was within the debounce time
        }
        else
        {
            lastPress = now;
        }
    }
    if (total_frames == 0 && !skipRequested)
    {
        Serial.printf("ERROR: No complete JPEG frame read from %s (position=%lu, size=%lu)\n",
                      mjpegFilename, (unsigned long)mjpegFile.position(), (unsigned long)mjpegFile.size());
    }
    skipRequested = false;

    unsigned long time_used = millis() - start_ms;
    Serial.println(F("MJPEG end"));
    mjpegFile.close();
    skipRequested = false; // ready for next video
    float percent_per_ms = time_used > 0 ? 100.0 / time_used : 0.0;
    float fps = time_used > 0 ? 1000.0 * total_frames / time_used : 0.0;
    total_decode_video -= total_show_video;
    Serial.printf("Total frames: %d\n", total_frames);
    Serial.printf("Time used: %lu ms\n", time_used);
    Serial.printf("Average FPS: %0.1f\n", fps);
    Serial.printf("Read MJPEG: %lu ms (%0.1f %%)\n", total_read_video, total_read_video * percent_per_ms);
    Serial.printf("Decode video: %lu ms (%0.1f %%)\n", total_decode_video, total_decode_video * percent_per_ms);
    Serial.printf("Show video: %lu ms (%0.1f %%)\n", total_show_video, total_show_video * percent_per_ms);
    if (total_frames > 0)
    {
        Serial.printf("Video size (wxh): %d×%d, scale factor=%d\n",mjpeg.getWidth(),mjpeg.getHeight(),mjpeg.getScale());
    }
}

const char *sdCardTypeName(uint8_t cardType)
{
    switch (cardType)
    {
    case CARD_NONE:
        return "NONE";
    case CARD_MMC:
        return "MMC";
    case CARD_SD:
        return "SDSC";
    case CARD_SDHC:
        return "SDHC";
    default:
        return "UNKNOWN";
    }
}

String buildMjpegPath(const String &fileName)
{
    if (fileName.startsWith("/"))
    {
        return fileName;
    }
    if (fileName.indexOf('/') >= 0)
    {
        return "/" + fileName;
    }
    return String(MJPEG_FOLDER) + "/" + fileName;
}

bool isMjpegVideoFileName(const String &fileName)
{
    int slashIndex = fileName.lastIndexOf('/');
    String baseName = slashIndex >= 0 ? fileName.substring(slashIndex + 1) : fileName;
    return baseName.endsWith(".mjpeg") && !baseName.startsWith("._");
}

void printFileReadTest(fs::File &file)
{
    uint8_t bytes[4] = {0, 0, 0, 0};
    int bytesRead = file.read(bytes, sizeof(bytes));
    Serial.printf("read=%d first bytes=%02X %02X %02X %02X\n",
                  bytesRead,
                  bytes[0],
                  bytes[1],
                  bytes[2],
                  bytes[3]);
}

void runSdDiagnostics()
{
    if (!ENABLE_SD_DIAGNOSTICS)
    {
        return;
    }

    Serial.println("SD diagnostics start");
    uint8_t cardType = SD.cardType();
    Serial.printf("Card type: %s\n", sdCardTypeName(cardType));
    Serial.printf("Card size: %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));

    File mjpegDir = SD.open(MJPEG_FOLDER);
    if (!mjpegDir)
    {
        Serial.printf("Diagnostic: failed to open %s folder\n", MJPEG_FOLDER);
        Serial.println("SD diagnostics end");
        return;
    }
    if (!mjpegDir.isDirectory())
    {
        Serial.printf("Diagnostic: %s is not a folder\n", MJPEG_FOLDER);
        mjpegDir.close();
        Serial.println("SD diagnostics end");
        return;
    }

    String firstMjpegPath = "";
    while (true)
    {
        File entry = mjpegDir.openNextFile();
        if (!entry)
        {
            break;
        }

        String name = entry.name();
        Serial.printf("entry: '%s', size=%lu, dir=%d\n",
                      name.c_str(),
                      (unsigned long)entry.size(),
                      entry.isDirectory());

        if (firstMjpegPath.length() == 0 && !entry.isDirectory() && isMjpegVideoFileName(name))
        {
            firstMjpegPath = buildMjpegPath(name);
            Serial.print("openNextFile read test: ");
            printFileReadTest(entry);
        }

        entry.close();
    }
    mjpegDir.close();

    if (firstMjpegPath.length() == 0)
    {
        Serial.printf("Diagnostic: no .mjpeg file found in %s\n", MJPEG_FOLDER);
        Serial.println("SD diagnostics end");
        return;
    }

    Serial.println("Writing to display before SD direct-open test");
    gfx->fillScreen(RGB565_BLACK);
    Serial.printf("Direct-open test after display write: %s\n", firstMjpegPath.c_str());
    File directFile = SD.open(firstMjpegPath.c_str(), FILE_READ);
    if (!directFile || directFile.isDirectory())
    {
        Serial.printf("Direct-open result: FAIL for %s\n", firstMjpegPath.c_str());
    }
    else
    {
        Serial.printf("Direct-open result: OK, size=%lu, ", (unsigned long)directFile.size());
        printFileReadTest(directFile);
    }
    directFile.close();
    Serial.println("SD diagnostics end");
}

// Read the mjpeg file list in the mjpeg folder of the SD card
bool loadMjpegFilesList()
{
    mjpegCount = 0;
    File mjpegDir = SD.open(MJPEG_FOLDER);
    if (!mjpegDir)
    {
        Serial.printf("ERROR: Failed to open %s folder\n", MJPEG_FOLDER);
        return false;
    }
    if (!mjpegDir.isDirectory())
    {
        Serial.printf("ERROR: %s is not a folder\n", MJPEG_FOLDER);
        mjpegDir.close();
        return false;
    }
    while (true)
    {
        File file = mjpegDir.openNextFile();
        if (!file)
            break;
        if (!file.isDirectory())
        {
            String name = file.name();
            if (isMjpegVideoFileName(name))
            {
                if (file.size() == 0)
                {
                    Serial.printf("WARNING: Skipping empty MJPEG file: %s\n", name.c_str());
                }
                else
                {
                    mjpegFileList[mjpegCount] = buildMjpegPath(name);
                    mjpegFileSizes[mjpegCount] = file.size();
                    mjpegCount++;
                }
            }
        }
        file.close();
        if (mjpegCount >= MAX_FILES)
            break;
    }
    mjpegDir.close();
    Serial.printf("%d mjpeg files read\n", mjpegCount);
    // Optionally, print out each file's size for debugging:
    for (int i = 0; i < mjpegCount; i++)
    {
        Serial.printf("File %d: %s, Size: %lu bytes (%s)\n", i, mjpegFileList[i].c_str(), mjpegFileSizes[i],formatBytes(mjpegFileSizes[i]).c_str());
    }
    if (mjpegCount == 0)
    {
        Serial.printf("ERROR: No non-empty .mjpeg files found in %s\n", MJPEG_FOLDER);
    }
    return mjpegCount > 0;
}

// Function helper display sizes on the serial monitor
String formatBytes(size_t bytes)
{
    if (bytes < 1024)
    {
        return String(bytes) + " B";
    }
    else if (bytes < (1024 * 1024))
    {
        return String(bytes / 1024.0, 2) + " KB";
    }
    else
    {
        return String(bytes / 1024.0 / 1024.0, 2) + " MB";
    }
}
