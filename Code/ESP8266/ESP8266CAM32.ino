#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Servo.h>
#include <SoftwareSerial.h>
const char* apSSID = "SystemAutoDetected";
const char* apPassword = "ESP8266ESP32CAM";
IPAddress apIP(192, 168, 4, 2);       // Задаём статический IP для ESP8266
IPAddress apSubnet(255, 255, 255, 0); // Маска подсети
#define SERVO_X_PIN D5
#define SERVO_Y_PIN D6
Servo servoX;
Servo servoY;
SoftwareSerial mySerial(13, 15); // RX = GPIO13, TX = GPIO15 (TX не используется)
AsyncWebServer server(80);
int currentX = 0;
int currentY = 0;
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP8266 Web Server</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }
    h1 { color: #333; }
    p { font-size: 1.2em; }
  </style>
</head>
<body>
  <h1>ESP8266 Асинхронный Веб-Сервер</h1>
  <p>Сервер получает координаты для управления.</p>
  <p>Текущие координаты:</p>
  <p>X: <span id="coordX">0</span>, Y: <span id="coordY">0</span></p>
  <script>
    function updateCoordinates() {
      fetch('/get-coordinates')
        .then(response => response.json())
        .then(data => {
          document.getElementById('coordX').textContent = data.x;
          document.getElementById('coordY').textContent = data.y;
        })
        .catch(error => console.error('Error:', error));
    }
    setInterval(updateCoordinates, 500);
  </script>
</body>
</html>
)rawliteral";

void smoothMoveServo(Servo &servo, int currentAngle, int targetAngle, int stepDelay = 10, int stepSize = 1) {
  if (currentAngle < targetAngle) {
    for (int angle = currentAngle; angle <= targetAngle; angle += stepSize) {
      servo.write(angle);
      delay(stepDelay); // Небольшая задержка для плавного движения
    }
  } else {
    for (int angle = currentAngle; angle >= targetAngle; angle -= stepSize) {
      servo.write(angle);
      delay(stepDelay);
    }
  }
}


int lastX = -1;  // Переменные для хранения предыдущих координат
int lastY = -1;
void moveToRegion(int x, int y) {
  const int imageWidth = 1024;          // Ширина изображения
  const int imageHeight = 768;         // Высота изображения
  const int SERVO_MIN_ANGLE = 0;       // Минимальный угол сервопривода
  const int SERVO_MAX_ANGLE = 180;     // Максимальный угол сервопривода
  const float CAMERA_VIEW_ANGLE_X = 25.0; // Угол обзора камеры по горизонтали
  const float CAMERA_VIEW_ANGLE_Y = 20.0; // Угол обзора камеры по вертикали

  const int centerX = imageWidth / 2;  // Центр по оси X
  const int centerY = imageHeight / 2; // Центр по оси Y

  if (x == lastX && y == lastY) {
    // Если координаты не изменились, ничего не делаем
    // Serial.printf("Координаты не изменились: X = %d, Y = %d\n", x, y);
    return;
  }

  // Сохраняем новые координаты
  lastX = x;
  lastY = y;

  // Определение смещения от центра
  int deltaX = x - centerX;
  int deltaY = centerY - y; // Инверсия по Y, так как камера сверху

  // Инвертирование смещения по оси X, если необходимо
  deltaX = -deltaX;

  // Преобразование смещения в углы с уменьшением чувствительности
  float angleOffsetX = ((float)deltaX / centerX) * (CAMERA_VIEW_ANGLE_X / 2) * 0.5;  // Уменьшен коэффициент
  float angleOffsetY = ((float)deltaY / centerY) * (CAMERA_VIEW_ANGLE_Y / 2) * 0.5;  // Уменьшен коэффициент

  // Получение текущих углов сервоприводов
  int currentServoXAngle = servoX.read();
  int currentServoYAngle = servoY.read();

  // Вычисление целевых углов
  int targetServoXAngle = currentServoXAngle + angleOffsetX * (SERVO_MAX_ANGLE / CAMERA_VIEW_ANGLE_X);
  int targetServoYAngle = currentServoYAngle + angleOffsetY * (SERVO_MAX_ANGLE / CAMERA_VIEW_ANGLE_Y);

  // Ограничение углов в допустимом диапазоне
  targetServoXAngle = constrain(targetServoXAngle, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
  targetServoYAngle = constrain(targetServoYAngle, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);

  // Добавлен порог для минимального отклонения
  const int MIN_MOVE_THRESHOLD = 5;  // Минимальное изменение для движения

  // Если отклонение от центра превышает порог, двигаем сервоприводы
  if (abs(deltaX) > MIN_MOVE_THRESHOLD || abs(deltaY) > MIN_MOVE_THRESHOLD) {
    // Наведение сервоприводов
    smoothMoveServo(servoX, currentServoXAngle, targetServoXAngle);
    smoothMoveServo(servoY, currentServoYAngle, targetServoYAngle);


    // Лог для отладки
    // Serial.printf("Наведение завершено: ServoX = %d, ServoY = %d (Координаты: X = %d, Y = %d)\n",
                  // targetServoXAngle, targetServoYAngle, x, y);
  } else {
    // Serial.printf("Лазер в центре, нет необходимости в движении: ServoX = %d, ServoY = %d (Координаты: X = %d, Y = %d)\n",
                  // currentServoXAngle, currentServoYAngle, x, y);
  }
}


void setup() {
  Serial.begin(115200);
  mySerial.begin(115200);    // Виртуальный Serial для ESP32-CAM
  servoX.attach(SERVO_X_PIN);
  servoY.attach(SERVO_Y_PIN);
  servoX.write(0);
  servoY.write(0);
  WiFi.softAPConfig(apIP, apIP, apSubnet);
  WiFi.softAP(apSSID, apPassword);
  Serial.println("Точка доступа создана!");
  Serial.print("Название сети: ");
  Serial.println(apSSID);
  Serial.print("IP-адрес: ");
  Serial.println(WiFi.softAPIP());
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=UTF-8", index_html);
  });

  server.on("/get-coordinates", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);
    doc["x"] = currentX;
    doc["y"] = currentY;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.begin();
  Serial.println("Сервер запущен!");
}

void loop() {
  if (mySerial.available()) {
    String data = mySerial.readStringUntil('\n'); // Читаем строку до конца
    Serial.print("Принято по UART: ");
    Serial.println(data);
    mySerial.flush();
    int x, y;
    if (sscanf(data.c_str(), "X:%d,Y:%d", &x, &y) == 2) {
      currentX = x;
      currentY = y;
      moveToRegion(currentX, currentY);
    }
  }
}