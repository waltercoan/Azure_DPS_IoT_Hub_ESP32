# Azure IoT Hub ESP32 adaptado para DPS
## Criação dos certificados
- [Tutorial: Provisionar vários dispositivos X.509 usando grupos de registro](https://learn.microsoft.com/pt-br/azure/iot-dps/tutorial-custom-hsm-enrollment-group-x509?pivots=programming-language-ansi-c&WT.mc_id=IoT-MVP-5003638)

Seguir exatamente o tutorial da Microsoft abaixo IMPORTANTE: no passo 7 da etapa de criar certificados do dispositivo (https://learn.microsoft.com/pt-br/azure/iot-dps/tutorial-custom-hsm-enrollment-group-x509?pivots=programming-language-ansi-c#create-device-certificates&WT.mc_id=IoT-MVP-5003638), há um pequeno erro no script onde esta fixo a geração da chave privada para o device-02, caso queira gerar para outro device deve ser modificado o valor.

## Documentação da integração do DPS por MQTT
[Communicate with DPS using the MQTT protocol](https://learn.microsoft.com/en-us/azure/iot/iot-mqtt-connect-to-iot-dps?WT.mc_id=IoT-MVP-5003638)

## Adaptação do código
- arquivo iot_configs.h
  - IOT_CONFIG_WIFI_SSID: SSID da rede WIFI
  - IOT_CONFIG_WIFI_PASSWORD: SENHA da rede WIFI
  - IOT_CONFIG_DEVICE_CERT: deve ser copiada o certificado completo gerado no passo 4 do tutorial (https://learn.microsoft.com/pt-br/azure/iot-dps/tutorial-custom-hsm-enrollment-group-x509?pivots=programming-language-ansi-c#configure-the-custom-hsm-stub-code&WT.mc_id=IoT-MVP-5003638) anterior que foi gerada pelo comando
  ```
  sed -e 's/^/"/;$ !s/$/""\\n"/;$ s/$/"/' ./certs/device-02-full-chain.cert.pem
    ```
  IMPORTANTE: ADICIONAR a barra \ no final de cada linha
  - IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY: deve ser copiada a chave completa gerada no passo 5 do tutorial (https://learn.microsoft.com/pt-br/azure/iot-dps/tutorial-custom-hsm-enrollment-group-x509?pivots=programming-language-ansi-c#configure-the-custom-hsm-stub-code&WT.mc_id=IoT-MVP-5003638) anterior que foi gerada pelo comando
  ```
  sed -e 's/^/"/;$ !s/$/""\\n"/;$ s/$/"/' ./private/device-02.key.pem
    ```
  IMPORTANTE: ADICIONAR a barra \ no final de cada linha
  - IOT_CONFIG_DPS_FQDN "mqtts://global.azure-devices-provisioning.net"
  - IOT_CONFIG_DPS_SCOPE: valor do ID Scope encontrado na página overview do serviço Azure DPS
  - IOT_CONFIG_DPS_REGISTRATION_ID: valor do campo CN (nome comum) informado na geração do certificado do dispositivo no pasos 2 do tutorial (https://learn.microsoft.com/pt-br/azure/iot-dps/tutorial-custom-hsm-enrollment-group-x509?pivots=programming-language-ansi-c#create-device-certificates&WT.mc_id=IoT-MVP-5003638) 
  - IOT_CONFIG_DEVICE_ID: identico ao valor anterior IOT_CONFIG_DPS_REGISTRATION_ID

### License

Azure SDK for Embedded C is licensed under the [MIT](https://github.com/Azure/azure-sdk-for-c/blob/main/LICENSE) license.
