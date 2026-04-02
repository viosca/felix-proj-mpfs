# Felix OS - Eclipse Workspace Wrapper

This repository is the umbrella Eclipse project that binds [**Felix OS**](https://github.com/viosca/felix) to the **Microchip PolarFire SoC** bare-metal hardware library.

It contains the pre-configured `.project` and `.cproject` files needed to compile the OS perfectly using Eclipse CDT, utilizing Git Submodules to pull in the correct versions of the OS and the hardware abstraction layer (HAL).

## 🚀 Getting Started

### 1. Prerequisites

* **Git** installed on your system.

* **Eclipse CDT** (or Microchip SoftConsole).

* **RISC-V GNU Cross-Compiler** (`riscv64-unknown-elf-gcc`) added to your system `$PATH`.

### 2. Cloning the Repository (CRITICAL)

Because this project uses Git Submodules for the OS and the Microchip HAL, you **must** clone it recursively so Git downloads the actual files inside the folders.

Run this in your terminal:

    git clone --recursive https://github.com/yourusername/felix-proj-mpfs.git

*(If you already cloned it without that flag, run `git submodule update --init --recursive` inside the folder).*

### 3. Importing into Eclipse

Do not create a new project. We need to import the pre-configured workspace:

1. Open Eclipse.

2. Go to **File -> Import...**

3. Select **General -> Existing Projects into Workspace** and click **Next**.

4. Browse to the `felix-proj-mpfs` folder you just cloned.

5. **IMPORTANT:** Ensure that **"Copy projects into workspace" is UNCHECKED**.

6. Click **Finish**.

### 4. Compiling

1. In the Project Explorer, right-click the `felix-proj-mpfs` project.

2. Select **Clean Project**.

3. Click the **Build** icon (the Hammer) in the top toolbar.

The compiled executable (`.elf`) will be generated in the respective build folder (e.g., `LIM-Debug-DiscoveryKit/`).

## 🛠️ Project Architecture & Quirks

This project uses **Virtual Linked Folders** to keep the workspace clean without duplicating files.

* `src/platform` is a virtual link to the `mpfs-platform` submodule.

* `src/middleware/felix` is a virtual link to the `felix` submodule.

### Troubleshooting: The Orange `!` in Eclipse

If you open Eclipse and see a red/orange `!` over the `platform` or `felix` folders, it means Eclipse hasn't loaded the submodules into its internal cache yet.

* **Fix:** Right-click the root `felix-proj-mpfs` project and click **Refresh (F5)**. The virtual links will immediately resolve.

### Troubleshooting: `newlib_stubs.c` Conflicts

Felix OS includes its own POSIX-compliant system call router. To prevent "Multiple Definition" linking errors, Microchip's default bare-metal syscalls are deliberately ignored by the compiler.

* If you ever reset the project paths, ensure that `src/platform/mpfs_hal/startup_gcc/newlib_stubs.c` is **Excluded from Build** in the file properties.
