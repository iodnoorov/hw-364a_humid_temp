# Термогигрометр на базе ESP8266MOD
Репа с исходниками небольшого градусника на базе HW-364A, подключением к wifi и собственным веб-сервером на борту

Для получений показаний используется DHT22. Дата кинута на D7 (GPIO 13). 

http://device.local/status - возвращает статус в JSON.
Пример статуса
```json
{
  "device_name": "Kitchen_1",
  "wifi": {
    "wifi_connected": true,
    "ssid": "Home",
    "ip": "192.168.1.142",
    "rssi": -50
  },
  "uptime": {
    "uptime_seconds": 221,
    "uptime": "0d 00:03:41"
  },
  "status": {
    "display_on": true,
    "sensor_ok": true
  },
  "sensors": {
    "temperature_c": 25,
    "humidity": 47.8,
    "wifi_power": "Good"
  }
}
```
http://device.local/?display=on|off - включает|выключает дисплей.
