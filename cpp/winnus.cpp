#include <node.h>

#pragma warning (disable: 4068)

// Massive hack - not sure how to fix this with node-gyp?
#undef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN8


#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#include <bthdef.h>
#include <bthledef.h>
#include <bluetoothleapis.h>

#include <iostream>
#include <sstream>
#include <string>
#include <locale>

#define RETURN_ERR(text) {isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, text)));return;}

HANDLE hLEDevice = 0;
PBTH_LE_GATT_CHARACTERISTIC pCharacteristics = 0;
PBTH_LE_GATT_CHARACTERISTIC pTXCharacteristic = 0; // send to Espruino (member of pCharacteristics)
PBTH_LE_GATT_CHARACTERISTIC pRXCharacteristic = 0; // read from Espruino (member of pCharacteristics)

namespace demo {

  using v8::Context;
  using v8::Array;
  using v8::Exception;
  using v8::Function;
  using v8::FunctionCallbackInfo;
  using v8::FunctionTemplate;
  using v8::Isolate;
  using v8::Local;
  using v8::Number;
  using v8::Object;
  using v8::Persistent;
  using v8::String;
  using v8::Value;


  void HandleBLENotification(BTH_LE_GATT_EVENT_TYPE EventType, PVOID EventOutParameter, PVOID Context) {
  	printf("notification obtained ");
  	PBLUETOOTH_GATT_VALUE_CHANGED_EVENT ValueChangedEventParameters = (PBLUETOOTH_GATT_VALUE_CHANGED_EVENT)EventOutParameter;
  	if (ValueChangedEventParameters->CharacteristicValue->DataSize) {
  		char buf[32];
  		int s = ValueChangedEventParameters->CharacteristicValue->DataSize;
  		memcpy(buf, ValueChangedEventParameters->CharacteristicValue->Data, s);
  		buf[s] = 0;
  		printf("got value %s\n", buf);
  	}
  }


  void WINNUS_GetDevicePaths(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Array> array = Array::New(isolate);

    // NUS GUID
  	GUID BluetoothInterfaceGUID;
  	CLSIDFromString(TEXT(L"{6e400001-b5a3-f393-e0a9-e50e24dcca9e}"), &BluetoothInterfaceGUID);

    HDEVINFO hDI;
  	SP_DEVICE_INTERFACE_DATA did;
  	SP_DEVINFO_DATA dd;

  	hDI = SetupDiGetClassDevs(&BluetoothInterfaceGUID, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);

  	if (hDI == INVALID_HANDLE_VALUE)
  		RETURN_ERR("Unable to access Bluetooth adaptor");

  	did.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
  	dd.cbSize = sizeof(SP_DEVINFO_DATA);

  	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDI, NULL, &BluetoothInterfaceGUID, i, &did); i++)
  	{
  		DWORD size = 0;

  		if (!SetupDiGetDeviceInterfaceDetail(hDI, &did, NULL, 0, &size, 0))
  		{
  			int err = GetLastError();

  			if (err == ERROR_NO_MORE_ITEMS) break;

  			PSP_DEVICE_INTERFACE_DETAIL_DATA pInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)GlobalAlloc(GPTR, size);

  			pInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

  			if (!SetupDiGetDeviceInterfaceDetail(hDI, &did, pInterfaceDetailData, size, &size, &dd))
  				break;



        Local<Object> obj = Object::New(isolate);
        OLECHAR* bstrGuid;
        StringFromCLSID(did.InterfaceClassGuid, &bstrGuid);
        obj->Set(String::NewFromUtf8(isolate, "guid"), String::NewFromTwoByte(isolate, (const uint16_t *)bstrGuid));
        ::CoTaskMemFree(bstrGuid);

        //obj->Set(String::NewFromUtf8(isolate, "name"), String::NewFromUtf8(isolate, nameBuffer));
        obj->Set(String::NewFromUtf8(isolate, "path"), String::NewFromUtf8(isolate, pInterfaceDetailData->DevicePath));
        array->Set(array->Length(), obj);

  			GlobalFree(pInterfaceDetailData);
  		}
  	}

  	SetupDiDestroyDeviceInfoList(hDI);
    args.GetReturnValue().Set(array);
  }

  void WINNUS_GetDeviceNames(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    Local<Array> array = Array::New(isolate);

    HDEVINFO hDI;
  	SP_DEVINFO_DATA did;
  	DWORD i;

  	// Create a HDEVINFO with all present devices.
  	hDI = SetupDiGetClassDevs(&GUID_DEVCLASS_BLUETOOTH, NULL, NULL,  DIGCF_PRESENT);
  	if (hDI == INVALID_HANDLE_VALUE) {
      isolate->ThrowException(Exception::Error(
          String::NewFromUtf8(isolate, "Unable to access Bluetooth adaptor")));
  		return;
  	}

  	// Enumerate through all devices in Set.
  	did.cbSize = sizeof(SP_DEVINFO_DATA);
  	for (i = 0; SetupDiEnumDeviceInfo(hDI, i, &did); i++) {
  		bool hasError = false;

  		DWORD nameData;
  		PSTR nameBuffer = NULL;
  		DWORD nameBufferSize = 0;

  		while (!SetupDiGetDeviceRegistryProperty(
  			hDI,
  			&did,
  			SPDRP_FRIENDLYNAME,
  			&nameData,
  			(PBYTE)nameBuffer,
  			nameBufferSize,
  			&nameBufferSize))
  		{
  			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
  			{
  				if (nameBuffer) delete(nameBuffer);
  				nameBuffer = new char[nameBufferSize * 2];
  			}
  			else
  			{
  				hasError = true;
  				break;
  			}
  		}

  		DWORD addressData;
  		LPSTR addressBuffer = NULL;
  		DWORD addressBufferSize = 0;

  		while (!SetupDiGetDeviceRegistryProperty(
  			hDI,
  			&did,
  			SPDRP_HARDWAREID,
  			&addressData,
  			(PBYTE)addressBuffer,
  			addressBufferSize,
  			&addressBufferSize))
  		{
  			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
  			{
  				if (addressBuffer) delete(addressBuffer);
  				addressBuffer = new char[addressBufferSize * 2];
  			}
  			else
  			{
  				hasError = true;
  				break;
  			}
  		}

  		LPSTR deviceIdBuffer = NULL;
  		DWORD deviceIdBufferSize = 0;

  		while (!SetupDiGetDeviceInstanceId(
  			hDI,
  			&did,
  			deviceIdBuffer,
  			deviceIdBufferSize,
  			&deviceIdBufferSize))
  		{
  			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
  			{
  				if (deviceIdBuffer) delete(deviceIdBuffer);
  				deviceIdBuffer = new char[deviceIdBufferSize * 2];
  			}
  			else
  			{
  				hasError = true;
  				break;
  			}
  		}

  		if (hasError)
  			continue;

      Local<Object> obj = Object::New(isolate);
      OLECHAR* bstrGuid;
      StringFromCLSID(did.ClassGuid, &bstrGuid);
      obj->Set(String::NewFromUtf8(isolate, "guid"), String::NewFromTwoByte(isolate, (const uint16_t *)bstrGuid));
      ::CoTaskMemFree(bstrGuid);
      obj->Set(String::NewFromUtf8(isolate, "name"), String::NewFromUtf8(isolate, nameBuffer));
      obj->Set(String::NewFromUtf8(isolate, "addr"), String::NewFromUtf8(isolate, addressBuffer));
      obj->Set(String::NewFromUtf8(isolate, "deviceId"), String::NewFromUtf8(isolate, deviceIdBuffer));
      array->Set(array->Length(), obj);
      delete nameBuffer;
      delete addressBuffer;
      delete deviceIdBuffer;
  	}
    args.GetReturnValue().Set(array);
  }

  void WINNUS_Connect(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();

    if (hLEDevice)
      RETURN_ERR("Already connected");

    // Check the number of arguments passed.
    if (args.Length() < 1) {
      // Throw an Error that is passed back to JavaScript
      isolate->ThrowException(Exception::TypeError(
          String::NewFromUtf8(isolate, "One argument expected")));
      return;
    }

    v8::String::Utf8Value pathArg(args[0]->ToString());

    hLEDevice = CreateFile(
      *pathArg,
      GENERIC_WRITE | GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      NULL,
      OPEN_EXISTING,
      0,
      NULL);
    if (!hLEDevice)
      RETURN_ERR("Couldn't open BLE device");

      // Step 2: Get a list of services that the device advertises
  	// first send 0, NULL as the parameters to BluetoothGATTServices inorder to get the number of
  	// services in serviceBufferCount
  	USHORT serviceBufferCount;
  	////////////////////////////////////////////////////////////////////////////
  	// Determine Services Buffer Size
  	////////////////////////////////////////////////////////////////////////////

  	HRESULT hr = BluetoothGATTGetServices(
  		hLEDevice,
  		0,
  		NULL,
  		&serviceBufferCount,
  		BLUETOOTH_GATT_FLAG_NONE);

  	if (HRESULT_FROM_WIN32(ERROR_MORE_DATA) != hr) {
  		printf("BluetoothGATTGetServices - Buffer Size %d\n", hr);
      RETURN_ERR("BluetoothGATTGetServices");
  	}

  	PBTH_LE_GATT_SERVICE pServiceBuffer = (PBTH_LE_GATT_SERVICE)
  		malloc(sizeof(BTH_LE_GATT_SERVICE) * serviceBufferCount);

  	if (NULL == pServiceBuffer) {
  		RETURN_ERR("pServiceBuffer out of memory\n");
  	}
  	else {
  		RtlZeroMemory(pServiceBuffer,
  			sizeof(BTH_LE_GATT_SERVICE) * serviceBufferCount);
  	}

    ////////////////////////////////////////////////////////////////////////////
    	// Retrieve Services
    	////////////////////////////////////////////////////////////////////////////

    	USHORT numServices;
    	hr = BluetoothGATTGetServices(
    		hLEDevice,
    		serviceBufferCount,
    		pServiceBuffer,
    		&numServices,
    		BLUETOOTH_GATT_FLAG_NONE);

    	if (S_OK != hr) {
    		printf("BluetoothGATTGetServices - Buffer Size %d\n", hr);
        RETURN_ERR("BluetoothGATTGetServices");
    	}

    	// Step 3: now get the list of charactersitics. note how the pServiceBuffer is required from step 2
    	////////////////////////////////////////////////////////////////////////////
    	// Determine Characteristic Buffer Size
    	////////////////////////////////////////////////////////////////////////////

    	USHORT charBufferSize;
    	hr = BluetoothGATTGetCharacteristics(
    		hLEDevice,
    		pServiceBuffer,
    		0,
    		NULL,
    		&charBufferSize,
    		BLUETOOTH_GATT_FLAG_NONE);

    	if (HRESULT_FROM_WIN32(ERROR_MORE_DATA) != hr) {
    		printf("BluetoothGATTGetCharacteristics - Buffer Size %d\n", hr);
        RETURN_ERR("BluetoothGATTGetCharacteristics");
    	}

    	if (charBufferSize > 0) {
    		pCharacteristics = (PBTH_LE_GATT_CHARACTERISTIC)
    			malloc(charBufferSize * sizeof(BTH_LE_GATT_CHARACTERISTIC));

    		if (NULL == pCharacteristics) {
    			RETURN_ERR("pCharBuffer out of memory\n");
    		}
    		else {
    			RtlZeroMemory(pCharacteristics,
    				charBufferSize * sizeof(BTH_LE_GATT_CHARACTERISTIC));
    		}

    		////////////////////////////////////////////////////////////////////////////
    		// Retrieve Characteristics
    		////////////////////////////////////////////////////////////////////////////
    		USHORT numChars;
    		hr = BluetoothGATTGetCharacteristics(
    			hLEDevice,
    			pServiceBuffer,
    			charBufferSize,
    			pCharacteristics,
    			&numChars,
    			BLUETOOTH_GATT_FLAG_NONE);

    		if (S_OK != hr) {
    			printf("BluetoothGATTGetCharacteristics - Actual Data %d\n", hr);
          RETURN_ERR("BluetoothGATTGetCharacteristics");
    		}

    		if (numChars != charBufferSize) {
    			RETURN_ERR("buffer size and buffer size actual size mismatch\n");
    		}
    	}


    	PBTH_LE_GATT_CHARACTERISTIC rxGattChar = 0;
    	PBTH_LE_GATT_CHARACTERISTIC txGattChar = 0;
    	for (int ii = 0; ii <charBufferSize; ii++) {
    		PBTH_LE_GATT_CHARACTERISTIC currGattChar;
    		currGattChar = &pCharacteristics[ii];

    		if (currGattChar->CharacteristicUuid.Value.ShortUuid==2) {
    			// TX characteristic
    			pTXCharacteristic = currGattChar;
    		}

    		if (currGattChar->CharacteristicUuid.Value.ShortUuid == 3) {
    			// RX characteristic
    			pRXCharacteristic = currGattChar;
    			///////////////////////////////////////////////////////////////////////////
    			// Determine Descriptor Buffer Size
    			////////////////////////////////////////////////////////////////////////////
    			USHORT descriptorBufferSize;
    			hr = BluetoothGATTGetDescriptors(
    				hLEDevice,
    				currGattChar,
    				0,
    				NULL,
    				&descriptorBufferSize,
    				BLUETOOTH_GATT_FLAG_NONE);

    			if (HRESULT_FROM_WIN32(ERROR_MORE_DATA) != hr) {
    				printf("BluetoothGATTGetDescriptors - Buffer Size %d\n", hr);
            RETURN_ERR("BluetoothGATTGetDescriptors");
    			}

    			PBTH_LE_GATT_DESCRIPTOR pDescriptorBuffer;
    			if (descriptorBufferSize > 0) {
    				pDescriptorBuffer = (PBTH_LE_GATT_DESCRIPTOR)
    					malloc(descriptorBufferSize
    						* sizeof(BTH_LE_GATT_DESCRIPTOR));

    				if (NULL == pDescriptorBuffer) {
    					RETURN_ERR("pDescriptorBuffer out of memory\n");
    				}
    				else {
    					RtlZeroMemory(pDescriptorBuffer, descriptorBufferSize);
    				}

    				////////////////////////////////////////////////////////////////////////////
    				// Retrieve Descriptors
    				////////////////////////////////////////////////////////////////////////////

    				USHORT numDescriptors;
    				hr = BluetoothGATTGetDescriptors(
    					hLEDevice,
    					currGattChar,
    					descriptorBufferSize,
    					pDescriptorBuffer,
    					&numDescriptors,
    					BLUETOOTH_GATT_FLAG_NONE);

    				if (S_OK != hr) {
    					printf("BluetoothGATTGetDescriptors - Actual Data %d\n", hr);
              RETURN_ERR("BluetoothGATTGetDescriptors");
    				}

    				if (numDescriptors != descriptorBufferSize) {
    					RETURN_ERR("buffer size and buffer size actual size mismatch\n");
    				}

    				for (int kk = 0; kk < numDescriptors; kk++) {
    					PBTH_LE_GATT_DESCRIPTOR  currGattDescriptor = &pDescriptorBuffer[kk];
    					////////////////////////////////////////////////////////////////////////////
    					// Determine Descriptor Value Buffer Size
    					////////////////////////////////////////////////////////////////////////////
    					USHORT descValueDataSize;
    					hr = BluetoothGATTGetDescriptorValue(
    						hLEDevice,
    						currGattDescriptor,
    						0,
    						NULL,
    						&descValueDataSize,
    						BLUETOOTH_GATT_FLAG_NONE);

    					if (HRESULT_FROM_WIN32(ERROR_MORE_DATA) != hr) {
    						printf("BluetoothGATTGetDescriptorValue - Buffer Size %d\n", hr);
                RETURN_ERR("BluetoothGATTGetDescriptorValue");
    					}

    					PBTH_LE_GATT_DESCRIPTOR_VALUE pDescValueBuffer = (PBTH_LE_GATT_DESCRIPTOR_VALUE)malloc(descValueDataSize);

    					if (NULL == pDescValueBuffer) {
    						RETURN_ERR("pDescValueBuffer out of memory\n");
    					}
    					else {
    						RtlZeroMemory(pDescValueBuffer, descValueDataSize);
    					}

    					////////////////////////////////////////////////////////////////////////////
    					// Retrieve the Descriptor Value
    					////////////////////////////////////////////////////////////////////////////

    					hr = BluetoothGATTGetDescriptorValue(
    						hLEDevice,
    						currGattDescriptor,
    						(ULONG)descValueDataSize,
    						pDescValueBuffer,
    						NULL,
    						BLUETOOTH_GATT_FLAG_NONE);
    					if (S_OK != hr) {
    						printf("BluetoothGATTGetDescriptorValue - Actual Data %d\n", hr);
                RETURN_ERR("BluetoothGATTGetDescriptorValue");
    					}
    					// you may also get a descriptor that is read (and not notify) andi am guessing the attribute handle is out of limits
    					// we set all descriptors that are notifiable to notify us via IsSubstcibeToNotification
    					if (currGattDescriptor->AttributeHandle < 255) {
    						BTH_LE_GATT_DESCRIPTOR_VALUE newValue;

    						RtlZeroMemory(&newValue, sizeof(newValue));

    						newValue.DescriptorType = ClientCharacteristicConfiguration;
    						newValue.ClientCharacteristicConfiguration.IsSubscribeToNotification = TRUE;

    						hr = BluetoothGATTSetDescriptorValue(
    							hLEDevice,
    							currGattDescriptor,
    							&newValue,
    							BLUETOOTH_GATT_FLAG_NONE);
    						if (S_OK != hr) {
    							printf("BluetoothGATTGetDescriptorValue - Actual Data %d\n", hr);
                  RETURN_ERR("BluetoothGATTGetDescriptorValue");
    						}
    					}
    				}
    			}

    			// set the appropriate callback function when the descriptor change value
    			BLUETOOTH_GATT_EVENT_HANDLE EventHandle;

    			if (currGattChar->IsNotifiable) {
    				printf("Setting Notification for ServiceHandle %d\n", currGattChar->ServiceHandle);
    				BTH_LE_GATT_EVENT_TYPE EventType = CharacteristicValueChangedEvent;

    				BLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION EventParameterIn;
    				EventParameterIn.Characteristics[0] = *currGattChar;
    				EventParameterIn.NumCharacteristics = 1;
    				hr = BluetoothGATTRegisterEvent(
    					hLEDevice,
    					EventType,
    					&EventParameterIn,
    					HandleBLENotification,
    					NULL,
    					&EventHandle,
    					BLUETOOTH_GATT_FLAG_NONE);

    				if (S_OK != hr) {
    					printf("BluetoothGATTRegisterEvent - Actual Data %d\n", hr);
              RETURN_ERR("BluetoothGATTRegisterEvent");
    				}
    			}
    		}
    	}
  }

  void WINNUS_Write(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();

    // Check the number of arguments passed.
    if (args.Length() < 1) {
      // Throw an Error that is passed back to JavaScript
      isolate->ThrowException(Exception::TypeError(
          String::NewFromUtf8(isolate, "One argument expected")));
      return;
    }

    v8::String::Utf8Value dataArg(args[0]->ToString());

    if (!hLEDevice)
      RETURN_ERR("Bluetooth connection not open");
    if (!pTXCharacteristic)
      RETURN_ERR("No TX characteristic found");

    PBTH_LE_GATT_CHARACTERISTIC_VALUE value = (PBTH_LE_GATT_CHARACTERISTIC_VALUE)new uint8_t[sizeof(ULONG)+dataArg.length()];
    value->DataSize = dataArg.length();
    memcpy(&value->Data, *dataArg, dataArg.length());
    HRESULT hr = BluetoothGATTSetCharacteristicValue(
      hLEDevice,
      pTXCharacteristic,
      value,
      0,
      BLUETOOTH_GATT_FLAG_WRITE_WITHOUT_RESPONSE
    );
    delete value;
  }

  void WINNUS_Disconnect(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    if (!hLEDevice)
      RETURN_ERR("Bluetooth connection not open");
    CloseHandle(hLEDevice);

    pRXCharacteristic = 0;
    pTXCharacteristic = 0;
    hLEDevice = 0;
    free(pCharacteristics);
    pCharacteristics = 0;

    if (GetLastError() != NO_ERROR &&
        GetLastError() != ERROR_NO_MORE_ITEMS)
    {
      RETURN_ERR("System error while closing");
    }
  }


void init(Local<Object> exports) {
  NODE_SET_METHOD(exports, "getDevicePaths", WINNUS_GetDevicePaths);
  NODE_SET_METHOD(exports, "getDeviceNames", WINNUS_GetDeviceNames);
  NODE_SET_METHOD(exports, "connect", WINNUS_Connect);
  NODE_SET_METHOD(exports, "disconnect", WINNUS_Disconnect);
  NODE_SET_METHOD(exports, "write", WINNUS_Write);
}

NODE_MODULE(winnus, init)

}  // namespace demo