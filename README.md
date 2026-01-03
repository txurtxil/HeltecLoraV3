# 📡 BambuLab LoRa Bridge & Remote Control

**Monitorización y Control Remoto de Largo Alcance para Impresoras Bambu Lab usando Heltec LoRa V3.**

Este proyecto soluciona el problema de tener la impresora 3D en una ubicación remota (ej. un trastero, sótano o garaje) donde no llega el WiFi de casa, pero se requiere control total y monitorización en tiempo real desde la vivienda.

![Esquema de Situación](edificio.png)
*Escenario del proyecto: Superando la barrera de distancia y obstáculos (3 pisos de hormigón) mediante tecnología LoRa.*


## 🌟 Características Principales

* **Puente LoRa de Largo Alcance:** Comunicación robusta mediante LoRa (868MHz) entre la impresora y el monitor.
* **Monitorización en Tiempo Real:** Visualización en pantalla OLED de % de progreso, tiempo restante, temperaturas (Nozzle/Cama), capa actual y velocidad del ventilador.
* **Control Total (Bidireccional):**
    * **Botón Físico:** Pausa/Reanudar (Click corto) y Parada de Emergencia (Click largo).
    * **Web App Local:** Joystick de control XY, movimiento Z, control de luz, cambio de velocidad de impresión y consola de comandos.
* **Gestión WiFi Inteligente:**
    * **Emisor:** Se conecta al WiFi de la impresora (o crea el suyo propio).
    * **Receptor:** Puede funcionar como Punto de Acceso (AP) o conectarse al WiFi de casa para acceder desde el móvil.
* **Configuración Vía Web:** Sin necesidad de recompilar. Configura IP, Access Code, Serial, WiFi y Potencia LoRa desde una interfaz web amigable.
* **Actualizaciones OTA:** Actualiza el firmware de ambos módulos vía WiFi sin cables.

---

## 🛠️ Hardware Necesario

| Componente | Cantidad | Descripción |
| :--- | :---: | :--- |
| **Heltec WiFi LoRa 32 V3** | 2 | Placas de desarrollo ESP32 + LoRa (una para Emisor, una para Receptor). |
| **Impresora Bambu Lab** | 1 | Compatible con A1, A1 Mini, P1P, P1S, X1C (Probado en A1 Mini). |
| **Antenas 868MHz** | 2 | Incluidas normalmente con las placas Heltec. |
| **Fuente de Alimentación** | 2 | USB-C (5V) para alimentar las placas. |
| **Cables USB-C** | 2 | Para programación y alimentación. |

## Nota para subir los binarios desde android:

Podemos subir los binarios en las placas desde android con la app ESP32_Flash usando el offset 0x0

Los datos los manda la impresora por mqtt puerto 8883, la conexión id para sacar los datos es : device/numeroserieinoresora/report

Hay que poner la impresora en modo lan y poner el usuario bblp y contraseña que sale en el modo LAN

---

## 📐 Arquitectura del Sistema

El sistema consta de dos módulos:

1.  **Módulo Emisor (Bridge):** Se coloca junto a la impresora. Se conecta a ella mediante MQTT (WiFi) y retransmite los datos vía LoRa. También recibe comandos LoRa y los traduce a G-Code/JSON para la impresora.
2.  **Módulo Receptor (Mando):** Se coloca en casa. Recibe los datos LoRa y los muestra en la pantalla OLED. Genera una Web de control y gestiona los botones físicos.

```mermaid
graph LR
    A[Bambu Lab Printer] -- MQTT (WiFi) --> B(Heltec Emisor V35)
    B -- LoRa 868MHz <--> C(Heltec Receptor V37)
    C -- WiFi AP/Client --> D[Smartphone / PC]
    D -- Web Interface --> C


