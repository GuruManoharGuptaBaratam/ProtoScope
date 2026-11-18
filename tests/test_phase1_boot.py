import pytest


@pytest.mark.esp32s3
def test_phase1_boot_and_console(dut):
    """Board-only foundation regression, also required by the Phase 2 image."""
    dut.expect_exact("ProtoScope Phase 1 foundation ready", timeout=20)
    dut.write("status")
    dut.expect_exact("ProtoScope status", timeout=5)
    dut.expect_exact("psram_initialized: yes", timeout=5)
    dut.expect_exact("psram_size_bytes: 8388608", timeout=5)
    dut.expect_exact("uart_active=no", timeout=5)
    dut.expect_exact("can_active=no", timeout=5)
    dut.expect_exact("rs485_active=no", timeout=5)
    dut.expect_exact("i2c_active=no", timeout=5)
    dut.expect_exact("spi_active=no", timeout=5)
    dut.expect_exact("routes_enabled=0", timeout=5)
    dut.write("heap")
    dut.expect(r"free_heap_bytes=\d+ minimum_free_heap_bytes=\d+", timeout=5)
