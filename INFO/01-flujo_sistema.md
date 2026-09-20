#  Documentación de Especificaciones Técnicas y de Interfaz: Dispensador de Detergentes

Este documento detalla paso a paso la arquitectura de control, el comportamiento de la interfaz de usuario con el HMI pantalla TFT 2.4" qeu esta aciopaldo con el ARDUINO UNO, la gestión de botones físicos de acción directa y el control del hardware periférico (Monedero, PLC y Servidor Web).

## LA PLACA DE CONTROL ESTA HECHO EN PCB TIENE ENTRAD E 12 VOLTISO PARA PUSLADORES Y OPTOACOPALDO A 3.3V PAR ALE ESP32 Y LA REGULACION YGUAL LOS PINES DE CADA ACTUAODR COMPOENTE ESTA ESPECIFICADO EN EL README DE pines.md otra seccion.


---

##   1. Arquitectura de Control y Máquina de Estados (FSM)

El sistema opera bajo una **Máquina de Estados Finitos (FSM)** estricta. Esto significa que el ESP32 se encuentra siempre en un **único estado a la vez**, dictando de forma centralizada qué componentes están activos, qué botones se escuchan y qué pasarelas de pago están permitidas, se comunica con el arduino uno por SERIAL2 y el arudino por el serial defecto el aislameinto de votlaje esta implementado en la placa. 

Resumen gerenal:
- Metodo de pago (QR Y moneda)
- elige moneda  selecciona el producto y  la cantidad y paga con moneda inserta  y confirma el pago y  le habilita el boton de dispensar de su producto, si no le alcanzo moneda se quedar ahi no se eliminar ygual si le sobro estar la diferecnia pero simepre estar habra otro boton para volver al inicio dodne puede elgegir entre qr o moneda si eligue moneda y otro producto digmaos mas barato eso comprar
- elige QR  se levnata el WI-FI y el servidor web  recien , en la ptalla mostrar un QR estatico del acceos a la red wifi escanea lo conecta al wifi y le meustrea en POPUP la pagina de simulacion una vez pagado mostar en el hmi los datoso de la compra simulado y el boton de volver al inico   otro qr si vuelve al incio apagar el wifi y el servidor y otro  qr  RECIBIR PETICIOES  PORQEU SE QUEDO EN  PAGO SIMULADO EN LA WEB  Y NOS E CMABIARA HASTA QUE DISPENSE SU PRODUCTO Y APRETE OTRO qr.

ESO SERIA  NIVEL GENEAL PROQUE BA CAMBIAR CIERTAS COSAS , PORQUE EL SISTEMA NO DA CAMBIOS SOLO RECIBE MONEDAS DE 1 2 Y 5 bs y los  los precios de lso productos son nuero enteros y pares par que la mitad no sea deciemal  si sobro dinero  no hacerla desaparecer ni caundo lo dormamos esto nivel genearl cone l bootn de depsertar despierta.

### Resumen de Estados ESPECIFICOS DEL  INTERFAZ TABLA



---

## 🎛️ 2. Diseño y Comportamiento de la Interfaz de Usuario HMI (UI)

Para evitar desajustes visuales y errores operativos, la interfaz **utiliza los botones  tactiles creado en el arduino uno toda la interza la mquian de estado el esp32 los manda que mostrar y el arudino envar que coas se esta haciendo el unico que es varible es el saldo de moneda que se va actuizando simpre lo dema es estatico en el hmi**. Se basa estrictamente comunicacion serial Uart 2 hilos TX Y RX a 9600 baudios

### Lógica de los Botones  (contra Errores)
1. **Detección por acciones (CLIK en la interfaz):** El sistema lee únicamente el clik como una accion al presionar, si  no la suelta el boton del hmi no deb hacer nada de accion esto par evitar errores de doble puslso digamo entrar o otro ventana directro.

3. **Comunciacion entre HMI y esp32:** Mi sigerencia es un codigo de confirmacion de informacion osea en cad ainterfaccion o ennvio que hace uno de los dos recibir confirmacion simpre para  si esque no confrimao la reccepcion volver a enviar esto apra evitar que lso datos se peirdadn como puede residir un confiramcion con o un caracter muy rapdio de enviar o estrign como OK eso se decidira en la elaboracion de codigo.

---

## 🔒 3. Control de Hardware y Seguridad de Pagos

### A. Gestión del Monedero Multicoin (`PIN_SET`)
* **bloqueo (`LOW` / 0V):** Envía una señal de bloqueo que obliga al monedero a rechazar cualquier moneda insertada, desviándola a la bandeja de devolución. Se activa en el Estado de Reposo, durante el flujo QR y en momentos críticos de cálculo matemático para evitar saltos de saldo.
* **Habilitado (`HIGH` / 3.3V):** Permite el paso de monedas. Sus pulsos físicos se leen mediante una interrupción (`ISR`) FALLING, en el ESP32 para actualizar el saldo en tiempo real en la Pantalla.

### B. Gestión del Servidor Web (Pagos por QR)
* El servidor asíncrono (`ESPAsyncWebServer`) se activa cuando pongan pago pro QR  la red Wi-Fi del ESP32, pero sus endpoints de cobro (`/api/pagar`) operan bajo un **candado de estado**.
* Si el sistema no se encuentra activamente en el **Estado 2 (QR - Espera)**, cualquier intento de pago desde un navegador web móvil será rechazado con un código HTTP de prohibición. Una vez que el usuario presiona el botón físico de QR en la máquina, el ESP32 abre la pasarela temporalmente hasta que se procesa el cobro o se cancela la operación.

---

## ⚙️ 4. Fase de Dispensado y Comunicación con el PLC

Independientemente de si el usuario pagó por Moneda o por QR, el proceso de entrega de líquido sigue un ciclo idéntico:

1. **Liberación de Boquilla:** El panel central se deshabilita y la HMI indica al usuario que presione el pulsador físico ubicado exactamente en la boquilla del producto correspondiente ESTO SBOOTNS SEA POR LLER POR INTERPCION O LEER ESTO SE DEBE ELIGIR LA MEJRO FORMA ASI TMABEIN COMO EL SENSOR DE DESPERTAR ES EL MISMO MECANISMO.
2. **Activación de Potencia:** Al presionar el botón de la boquilla, el ESP32 envía una señal de salida (`HIGH`) a través de la **placa de optoacopladores**, la cual conmuta la entrada digital del **PLC**.
3. **Bombeo y Medición:** El PLC activa su relé de 10A para encender la bomba de 12V. Simultáneamente, el sensor de flujo **YF-S201** correspondiente envía una cadena de pulsos al ESP32 por interrupción y lo mide y decide caundo parar la sslida hacia el plc los dos sensores de flulo el tercero no tiene va ser por tiempo ya que el producto es mucha viscosidad .
4. **Sensores de nivel de liquido son tres para cada bomba** estaran puestos arriba de la que extrae de la bomba la funcion es avisar que prodcutos mostrar en el hmi o WEB Ppara decidor que prodcutos mostrar o no ahroa este tien un filtro RC ya que puede dar cosas errores osea salpicarlo jugar hay yno hya para eso se deb hacer un timepo de sensado osea hay unactivacion y sensorar durantoe x segundo para no ver fallas ose afiltro por sofare AHORA HAY UN EXEPCION Y ESQUE CUANDO ESTAN  EL PRODUCTO Y SELECCIONES ESO Y ENPIEZE A DISPENSAR Y SE ACABE NO PUEDE PARAR EL DISPENSAOD YA QUE HAY MARGEN HACIA ARRIBA  SI O SI DBE ACABAR  CUANDO ESE SEA WR O MONEDA Y CUANDO TEMRINE EN LAPOXIMA COMPRAR AHI ACTILIZAR Y NO MOSTRAR ESTE PRODCUTO PARA DISPENSAR ESTOY ES MUY OBVIO NO
4. **Corte y Seguridad (Timeout):** 
   * Al alcanzar el número exacto de pulsos equivalentes al volumen comprado O POR TIMEPO HAY UNO QUE ES ASI , el ESP32 apaga la señal al PLC, deteniendo la bomba.
   
## TODO ES TEORICO ACTUALMENTE EN EL PROCESO PUEDE CAMBIAR DE ACUERDO A LAS SITUACIONES
