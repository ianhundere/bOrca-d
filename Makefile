all: release

.PHONY: debug
debug:
	@./tool build -d --portmidi orca

.PHONY: release
release:
	@./tool build --portmidi orca
	@echo "Executable program saved as: build/orca" >&2
	@echo "To run it, simply execute it:" >&2
	@echo "$$ build/orca" >&2

# The uConsole appliance build (Linux only): native ALSA MIDI output,
# hardened, PIE. tool exits 1 on other systems.
.PHONY: appliance
appliance:
	@./tool build --alsa --harden --pie orca
	@echo "Executable program saved as: build/orca" >&2
	@echo "To run it, simply execute it:" >&2
	@echo "$$ build/orca" >&2

.PHONY: clean
clean:
	@./tool clean
