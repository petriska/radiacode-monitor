# Third-party components

**Radiacode Monitor** (this application) is licensed under the **MIT License**
(see [`LICENSE`](LICENSE)).

Binary packages (Windows installer, macOS app bundle, etc.) may also ship
**shared libraries** that keep their own licenses. Those libraries are **not**
relicensed as MIT; only the application and its own source are MIT.

This file is a convenience summary. The upstream projects remain the
authoritative source for their license texts.

---

## Qt

| | |
|---|---|
| **Component** | Qt 6 (Core, Widgets, and plugins deployed with the app) |
| **Vendor** | The Qt Company / Qt Project |
| **Typical license** | **GNU LGPL v3** (open-source Qt builds) or a **commercial Qt license** if you build against a commercial kit |
| **How used** | Dynamically linked (`Qt6*.dll` / frameworks via `windeployqt` / `macdeployqt`) |
| **Website** | https://www.qt.io/ |
| **LGPL text** | https://www.gnu.org/licenses/lgpl-3.0.html |
| **Source** | https://code.qt.io/cgit/qt/ (or the Qt Online Installer sources for your exact version) |

### LGPL notes (binary distribution)

When distributing binaries linked to open-source Qt under LGPL:

1. State that the application uses Qt under LGPL (or commercial, if applicable).
2. Provide this notice and a way to obtain the LGPL text (link above is enough).
3. Provide a way to obtain corresponding Qt source for the version you ship
   (official Qt source download / code.qt.io is sufficient).
4. Keep Qt as **shared libraries** so a user can replace them with a compatible
   build (this project’s packaging already does that).

If you build and ship against a **commercial Qt license**, follow your Qt
commercial agreement instead of LGPL redistribution rules for those libraries.

---

## libusb

| | |
|---|---|
| **Component** | libusb-1.0 |
| **License** | **GNU LGPL v2.1** (or later, per upstream) |
| **How used** | Dynamically linked (`libusb-1.0.dll` / shared lib), via QtRadiacode USB transport |
| **Website** | https://libusb.info/ |
| **License text** | https://github.com/libusb/libusb/blob/master/COPYING |
| **Source** | https://github.com/libusb/libusb |

Same general LGPL redistribution ideas as Qt: notice, license text/link, source
availability, and prefer shared linking (as shipped by this project).

---

## QtRadiacode

| | |
|---|---|
| **Component** | QtRadiacode (`QtRadiacode` library) |
| **License** | **MIT** |
| **How used** | Dynamically linked companion library for RadiaCode USB/BLE protocol |
| **Repository** | https://github.com/petriska/qtradiacode |

See the library repository for its full `LICENSE` and protocol attribution notes.

---

## Protocol / community reference (not bundled source)

Device behaviour and the wire protocol were implemented with reference to
community documentation and projects (for example the Python **radiacode**
ecosystem and related open hardware/software notes). Those projects retain
their own licenses. **Radiacode Monitor / QtRadiacode reimplement the protocol
in Qt/C++** rather than vendoring their source wholesale.

---

## Trademarks

**RadiaCode** and related marks are property of their respective owners.
This project is an **unofficial community tool** and is **not affiliated with,
endorsed by, or sponsored by** the RadiaCode hardware manufacturer.
