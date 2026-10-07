Vendored copy of https://github.com/jnthas/Improv-WiFi-Library @ f02cb67 (MIT).

Vendored because its `library.properties` declares `depends=WiFi`, which makes
PlatformIO install the generic Arduino WiFi-shield library and shadow the
ESP32 core's own `WiFi.h`.
