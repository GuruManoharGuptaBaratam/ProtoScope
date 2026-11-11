import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "configure_n16r8", Path(__file__).resolve().parents[2] / "tools/configure_n16r8.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MemoryMigrationTests(unittest.TestCase):
    def test_existing_config_migrates_and_preserves_other_settings(self):
        old = ('CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y\n'
               'CONFIG_ESPTOOLPY_FLASHSIZE="8MB"\n'
               '# CONFIG_ESPTOOLPY_FLASHSIZE_16MB is not set\n'
               'CONFIG_SPIRAM_MODE_QUAD=y\n'
               '# CONFIG_SPIRAM_MODE_OCT is not set\n'
               'CONFIG_SPIRAM_IGNORE_NOTFOUND=y\n'
               'CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200\n'
               'CONFIG_PROTOSCOPE_HEALTH_PERIOD_MS=12000\n')
        new = module.n16r8_config(old)
        for expected in ('CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y',
                         'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"',
                         'CONFIG_SPIRAM_MODE_OCT=y',
                         '# CONFIG_SPIRAM_MODE_QUAD is not set',
                         '# CONFIG_ESPTOOLPY_FLASHSIZE_8MB is not set',
                         '# CONFIG_SPIRAM_IGNORE_NOTFOUND is not set',
                         'CONFIG_SPIRAM_MEMTEST=y',
                         'CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200',
                         'CONFIG_PROTOSCOPE_HEALTH_PERIOD_MS=12000'):
            self.assertEqual(new.splitlines().count(expected), 1)
        self.assertEqual(module.n16r8_config(new), new)

    def test_disabled_psram_is_enabled_without_touching_flash_mode(self):
        new = module.n16r8_config('# CONFIG_SPIRAM is not set\nCONFIG_ESPTOOLPY_FLASHMODE="dio"\n')
        self.assertIn('CONFIG_SPIRAM=y\n', new)
        self.assertIn('CONFIG_ESPTOOLPY_FLASHMODE="dio"\n', new)


if __name__ == "__main__":
    unittest.main()
