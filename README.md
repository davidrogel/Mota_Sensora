# Proyecto Mota Sensora

Proyecto para la creación de una Mota Sensora que permita leer los sensores acoplados.

### Dependencias ⚠️⚠️⚠️

La Mota depende del módulo que lee el sensor DHT11. Para descargar el proyecto junto a las dependencias usar:

```
git clone --recurse-submodules https://github.com/davidrogel/Mota_Sensora
```

## Sensores

| Sensor | Documentación |
|---|---|
| DHT11 | [Enlace](https://www.mouser.com/datasheet/2/758/DHT11-Technical-Data-Sheet-Translated-Version-1143054.pdf) |
| MQ-2 | [Enlace](http://gas-sensor.ru/pdf/combustible-gas-sensor.pdf) |
| MQ-3 | [Enlace](https://www.sparkfun.com/datasheets/Sensors/MQ-3.pdf) |

## Microcontrolador - ESP32


### Proyecto Base y otros

Se ha usado el proyecto de ejemplo **[ESP HTTP Client Example](https://github.com/espressif/esp-idf/tree/master/examples/protocols/esp_http_client)** como base para la creación del proyecto actual ya que ayuda a la hora de poder conectarse a una Wifi y trae ejemplos útiles de envio de datos a traves del protocolo HTTP.

A parte del proyecto mencionado antes, se ha usado el proyecto de ejemplo **[adc](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/adc/single_read/adc)** para la lectura de valores analógicos


### Configuración y uso

Dentro de la carpeta del proyecto, configurar el tipo de dispositivo y abrir el **menuconfig** para configurar la ssid y password del WiFi y la api-key de thingspeak. También es posible modificar el pin digital del sensor DHT11.
```
idf.py set-target esp32
idf.py menuconfig
```

Tras configurar, hacer build y flashear el programa
```
idf.py build
idf.py -p PORT flash
```

El parámetro **PORT** será el USB que tengais conectado al dispositivo, en linux se puede hacer lo siguiente para buscar el USB:
```
ls /dev/ttyUSB*
```

Por último podeis monitorizar lo que está pasando (obtener los datos de salida por consola) usando monitor:
```
idf.py -p PORT monitor
```

