#include "esp_camera.h"
#include <WiFi.h>
#define TXD_PIN 1  // TX (U0T) на ESP32-CAM
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"
#include <HTTPClient.h>
const char *ssid = "SystemAutoDetected";
const char *password = "ESP8266ESP32CAM";
const char* serverUrl = "http://192.168.4.2/coordinates";
IPAddress staticIP(192, 168, 4, 1);  // Статический IP-адрес для камеры
IPAddress gateway(192, 168, 4, 1);     // Шлюз (адрес ESP8266 в режиме AP)
IPAddress subnet(255, 255, 255, 0);    // Маска подсети
size_t positionX = 0;
size_t positionY = 0;
WiFiServer server(80);
#define TXD_PIN 1  // TX0 (U0T) на ESP32-CAM

void sendCoordinates(int x, int y) {
    Serial1.print("X:"); Serial1.print(x);
    Serial1.print(",Y:"); Serial1.println(y);
    if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"x\":" + String(x) + ",\"y\":" + String(y) + "}";
    int httpResponseCode = http.POST(payload);
    if (httpResponseCode > 0) {
      // Serial.printf("Response: %s\n", http.getString().c_str());
      // Serial.printf("Coordinates: X = %d | Y = %d", x, y);
    } else {
      // Serial.printf("Error on sending POST: %d\n", httpResponseCode);
    }
    http.end();
  } else {
    // Serial.println("WiFi disconnected!");
  }
}

void setCameraExposure(int level) {
    sensor_t *s = esp_camera_sensor_get();
    if (s != nullptr) {
        s->set_exposure_ctrl(s, 1); // Включение автоматической экспозиции (1 - включено, 0 - выключено)
        s->set_aec2(s, 0);          // Выключение усовершенствованного контроля экспозиции
        s->set_aec_value(s, level); // Установка уровня экспозиции (-2 до +2 или 0-120 в зависимости от камеры)
    }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  // Конфигурация камеры
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;  
  config.frame_size = FRAMESIZE_XGA;      
  config.jpeg_quality = 30;
  config.fb_count = 1;
  if (psramFound()) {
    // Serial.println("PSRAM detected.");
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    // Serial.println("PSRAM not found. Switching to DRAM.");
    config.fb_location = CAMERA_FB_IN_DRAM;
  }
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    // Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  setCameraExposure(-1);

  WiFi.config(staticIP, gateway, subnet);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  // Serial.println("\nWiFi connected.");
  // Serial.print("Use 'http://");
  // Serial.print(WiFi.localIP());
  // Serial.println("' to connect.");
  server.begin();
    Serial1.begin(115200, SERIAL_8N1, -1, TXD_PIN);  // UART TX на GPIO1
}
// void debugPixelConversion(uint16_t color565) {
//   uint8_t r, g, b;
//   RGB565toRGB888(color565, r, g, b);
//   Serial.printf("RGB565: 0x%04X -> RGB888: (%d, %d, %d)\n", color565, r, g, b);
// }
uint16_t toLittleEndian(uint16_t color) {
  return (color >> 8) | (color << 8); // Swap bytes if needed
}
void RGB565toRGB888(uint16_t color565, uint8_t &r, uint8_t &g, uint8_t &b) {
  color565 = toLittleEndian(color565); // Ensure little-endian representation
  r = ((color565 >> 11) & 0x1F) * 255 / 31; // Convert 5 bits to 8 bits
  g = ((color565 >> 5) & 0x3F) * 255 / 63;  // Convert 6 bits to 8 bits
  b = (color565 & 0x1F) * 255 / 31;         // Convert 5 bits to 8 bits
}
bool isColorInRange(uint8_t r, uint8_t g, uint8_t b,
                    uint8_t minR, uint8_t maxR,
                    uint8_t minG, uint8_t maxG,
                    uint8_t minB, uint8_t maxB) {
  return (r >= minR && r <= maxR &&
          g >= minG && g <= maxG &&
          b >= minB && b <= maxB);
}
void processFrame(camera_fb_t *fb,
                  uint8_t minR, uint8_t maxR,
                  uint8_t minG, uint8_t maxG,
                  uint8_t minB, uint8_t maxB) {
  if (!fb) {
    Serial.println("Invalid frame buffer!");
    return;
  }
  uint16_t *pixels = (uint16_t *)fb->buf; // RGB565 pixels
  size_t width = fb->width;
  size_t height = fb->height;
  // Debug the first 10 pixels
  // Serial.println("Debugging first 10 pixels:");
  // for (size_t i = 0; i < 10; i++) {
  //   debugPixelConversion(pixels[i]);
  // }
  bool detected = false;
  int laserX = 0, laserY = 0;
  for (size_t y = 0; y < height; y++) {
    for (size_t x = 0; x < width; x++) {
      size_t index = y * width + x;
      uint8_t r, g, b;
      RGB565toRGB888(pixels[index], r, g, b);
      if (isColorInRange(r, g, b, minR, maxR, minG, maxG, minB, maxB)) {
        Serial.printf("Laser detected at (%d, %d) with RGB (%d, %d, %d)\n", x, y, r, g, b);
        detected = true;
        laserX = x;
        laserY = y;
        positionX = x;
        positionY = y;
        break;
      }
    }
    if (detected) break;
  }
  if (detected) {
    
    drawCircleOutline(pixels, width, height, laserX, laserY, 10, 0xFFFF); // White outline
  } else {
    Serial.println("No laser detected.");
  }
}

void calculateSectorAverage(uint16_t *pixels, size_t width, size_t height,
                            size_t sectorX, size_t sectorY,
                            size_t sectorWidth, size_t sectorHeight,
                            uint8_t &avgR, uint8_t &avgG, uint8_t &avgB) {
  uint32_t sumR = 0, sumG = 0, sumB = 0;
  size_t pixelCount = 0;
  for (size_t y = sectorY; y < sectorY + sectorHeight; y++) {
    for (size_t x = sectorX; x < sectorX + sectorWidth; x++) {
      if (x >= width || y >= height) continue; // За пределами кадра
      size_t index = y * width + x;

      uint8_t r, g, b;
      RGB565toRGB888(pixels[index], r, g, b);
      sumR += r;
      sumG += g;
      sumB += b;
      pixelCount++;
    }
  }
  if (pixelCount > 0) {
    avgR = sumR / pixelCount;
    avgG = sumG / pixelCount;
    avgB = sumB / pixelCount;
  } else {
    avgR = avgG = avgB = 0;
  }
}

void drawCircleOutline(uint16_t *pixels, size_t width, size_t height, int centerX, int centerY, int radius, uint16_t color) {
  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      int distance = x * x + y * y;
      if (distance >= (radius - 1) * (radius - 1) && distance <= radius * radius) { 
        int posX = centerX + x;
        int posY = centerY + y;
        if (posX >= 0 && posX < width && posY >= 0 && posY < height) {
          pixels[posY * width + posX] = color; 
        }
      }
    }
  }
}

void sendJpegFrame(WiFiClient &client, camera_fb_t *fb) {
  uint8_t *jpeg_buf = NULL;
  size_t jpeg_len = 0;
  if (!frame2jpg(fb, 80, &jpeg_buf, &jpeg_len)) { 
    Serial.println("Failed to convert frame to JPEG");
    client.println("HTTP/1.1 500 Internal Server Error");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("Failed to convert frame to JPEG");
    return;
  }
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: image/jpeg");
  client.println("Content-Length: " + String(jpeg_len));
  client.println("Connection: close");
  client.println();
  client.write(jpeg_buf, jpeg_len);
  free(jpeg_buf);
}

void handleFrameProcessing() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Failed to capture frame");
    return;
  }

  // Определение диапазонов цвета
  uint8_t minR = 220, maxR = 255;
  uint8_t minG = 129, maxG = 195;
  uint8_t minB = 129, maxB = 195;

  // Обработка изображения
  processFrame(fb, minR, maxR, minG, maxG, minB, maxB);

  // Отправка координат
  sendCoordinates(positionX, positionY);

  // Освобождение памяти
  esp_camera_fb_return(fb);
  Serial.println("Frame processed.");
}

unsigned long lastTime = 0; // Время последней отправки
const unsigned long interval = 100; // Интервал 100 мс

void loop() {
  unsigned long currentTime = millis();

  // Проверка, прошло ли 100 мс
  if (currentTime - lastTime >= interval) {
    lastTime = currentTime; // Обновить время последней отправки

    handleFrameProcessing();

    // Обработка входящих HTTP-запросов
    WiFiClient client = server.available();
    if (!client) {
      return;
    }

    String request = client.readStringUntil('\r');
    Serial.println(request);
    client.flush();

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Failed to capture frame");
      client.println("HTTP/1.1 500 Internal Server Error");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.println("Failed to capture frame");
      client.stop();
      return;
    }

    // Отправка изображения и координат
    sendJpegFrame(client, fb);
    sendCoordinates(positionX, positionY);

    // Освобождение памяти
    esp_camera_fb_return(fb);
    client.stop();
    Serial.println("Image sent.");
  }
}




  // for (size_t i = 0; i < fb->len / 2; i++) {
  //   if(pixels[i] == 0xA5F9 ){
  //     Serial.println("Detected");
  //     break;
  //   }
  //   else{
  //     Serial.println("Not detected");
  //     break;
  //   }
  //   // pixels[i] = 0xE007; // Меняем порядок байтов: красный цвет в little-endian
  // }


// #FF0000
// 0x00F8 - красный
// 0xE007 - зеленый
// 0x1F00  - синий



// #define COLOR1 0x1FF8 // #F7FCF8 (little-endian)
// #define COLOR2 0x16F4 // #F3A0B2 (little-endian)
// #define COLOR3 0x11F9 // #F9878E (little-endian)

// void processFrame(camera_fb_t *fb) {
//   if (!fb) {
//     Serial.println("Invalid frame buffer!");
//     return;
//   }

//   uint16_t *pixels = (uint16_t *)fb->buf; // RGB565 пиксели
//   size_t width = fb->width;
//   size_t height = fb->height;

//   bool detected = false;
//   int centerX = 0, centerY = 0;

//   // Поиск точки в диапазоне цветов
//   for (size_t y = 0; y < height; y++) {
//     for (size_t x = 0; x < width; x++) {
//       size_t index = y * width + x;
//       if (pixels[index] == COLOR1 || pixels[index] == COLOR2 || pixels[index] == COLOR3) {
//         detected = true;
//         centerX = x;
//         centerY = y;
//         break;
//       }
//     }
//     if (detected) break;
//   }

//   if (detected) {
//     Serial.println("Color Detected!");

//     // Добавление обводки вокруг найденной точки
//     drawCircleOutline(pixels, width, height, centerX, centerY, 10, 0xFFFF); // Белая обводка
//   }
// }

// // Функция для рисования обводки круга (только контур)
// void drawCircleOutline(uint16_t *pixels, size_t width, size_t height, int centerX, int centerY, int radius, uint16_t color) {
//   for (int y = -radius; y <= radius; y++) {
//     for (int x = -radius; x <= radius; x++) {
//       int distance = x * x + y * y;
//       if (distance >= (radius - 1) * (radius - 1) && distance <= radius * radius) { // Толщина обводки 1 пиксель
//         int posX = centerX + x;
//         int posY = centerY + y;
//         if (posX >= 0 && posX < width && posY >= 0 && posY < height) {
//           pixels[posY * width + posX] = color; // Рисуем пиксель
//         }
//       }
//     }
//   }
// }

  // uint8_t minR = 230, maxR = 255; 
  // uint8_t minG = 150, maxG = 185;   
  // uint8_t minB = 150, maxB = 185; 



// FRAMESIZE_96X96      // 96x96 пикселей
// FRAMESIZE_QQVGA      // 160x120 пикселей
// FRAMESIZE_QCIF       // 176x144 пикселей
// FRAMESIZE_HQVGA      // 240x176 пикселей
// FRAMESIZE_240X240    // 240x240 пикселей
// FRAMESIZE_QVGA       // 320x240 пикселей
// FRAMESIZE_CIF        // 400x296 пикселей
// x       // 480x320 пикселей
// FRAMESIZE_VGA        // 640x480 пикселей
// FRAMESIZE_SVGA       //  
// FRAMESIZE_XGA        // 1024x768 пикселей
// FRAMESIZE_SXGA       // 1280x1024 пикселей
// FRAMESIZE_UXGA       // 1600x1200 пикселей

