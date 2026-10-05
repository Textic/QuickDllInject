# QuickDllInject - Plugin para System Informer

Un plugin para **System Informer** que añade un acceso directo **"Inject DLL..."** directamente en el menú contextual del clic derecho de cualquier proceso, eliminando la necesidad de ir a *Properties -> Modules -> Load DLL*.

---

## 🎯 Características

* **Acceso Directo con 1 Clic:** Haz clic derecho sobre cualquier proceso en la lista principal y verás la opción `Inject DLL...`.
* **Nativo y Seguro:** Invoca la función interna oficial `PhUiLoadDllProcess` de System Informer:
  * Abre el explorador de archivos nativo con filtro `.dll`.
  * Abre el proceso con los privilegios mínimos necesarios.
  * Inyecta la librería con `PhLoadDllProcess` mediante el driver o inyección remota.
  * Muestra retroalimentación y mensajes de error en caso de fallo.
* **Filtro de Procesos del Sistema:** Protege contra intentos accidentales de inyección en procesos protegidos del sistema (`System`, `Idle`).

---

## 🛠️ Estructura del Proyecto

* `main.c`: Implementación del plugin, registro en `DllMain` y suscripción a `GeneralCallbackProcessMenuInitializing` y `PluginCallbackMenuItem`.
* `QuickDllInject.h`: Encabezado principal del plugin.
* `resource.h`: Identificadores de menú y recursos.
* `version.rc`: Metadatos y versión de la DLL.
* `QuickDllInject.vcxproj`: Proyecto listo para compilar con Visual Studio 2022.
* `CMakeLists.txt`: Configuración alternativa con CMake.

---

## 🚀 Compilación e Instalación

### Método 1: Con la solución Plugins.sln (Recomendado)
1. Copia o crea un enlace simbólico de esta carpeta dentro de:
   `C:\Users\danie\Downloads\systeminformer\plugins\QuickDllInject`
2. Abre `C:\Users\danie\Downloads\systeminformer\plugins\Plugins.sln` en Visual Studio 2022.
3. Haz clic derecho en la solución -> **Agregar -> Proyecto existente...** y selecciona `QuickDllInject.vcxproj`.
4. Compila en configuración **Release** y arquitectura **x64**.
5. El binario resultante `QuickDllInject.dll` se colocará en la carpeta `plugins\` del directorio de salida.
6. Copia `QuickDllInject.dll` en:
   `C:\Program Files\SystemInformer\plugins\`
7. Abre System Informer y verás `QuickDllInject` activo en **Options -> Plugins**.

### Método 2: Con MSBuild desde la terminal
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "X:\QuickDllInject\QuickDllInject.vcxproj" /p:Configuration=Release /p:Platform=x64
```

---

## 📋 Uso

1. Abre **System Informer** (preferiblemente como Administrador).
2. Haz clic derecho sobre cualquier proceso en la vista de árbol.
3. Selecciona **Inject DLL...**.
4. Elige tu archivo `.dll` en la ventana emergente y ¡listo!
