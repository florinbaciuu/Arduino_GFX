#include "Arduino_ESP32QSPI.h"

#if defined(ESP32)

Arduino_ESP32QSPI::Arduino_ESP32QSPI(
    int8_t cs, int8_t sck, int8_t mosi, int8_t miso, int8_t quadwp, int8_t quadhd)
    : _cs(cs), _sck(sck), _mosi(mosi), _miso(miso), _quadwp(quadwp), _quadhd(quadhd)
{
}

bool Arduino_ESP32QSPI::begin(int32_t speed, int8_t dataMode)
{
  // set SPI parameters
  _speed = (speed == GFX_NOT_DEFINED) ? QSPI_FREQUENCY : speed;
  _dataMode = (dataMode == GFX_NOT_DEFINED) ? QSPI_SPI_MODE : dataMode;

  pinMode(_cs, OUTPUT);
  digitalWrite(_cs, HIGH); // disable chip select
  if (_cs >= 32)
  {
    _csPinMask = digitalPinToBitMask(_cs);
    _csPortSet = (PORTreg_t)&GPIO.out1_w1ts.val;
    _csPortClr = (PORTreg_t)&GPIO.out1_w1tc.val;
  }
  else
  {
    _csPinMask = digitalPinToBitMask(_cs);
    _csPortSet = (PORTreg_t)&GPIO.out_w1ts;
    _csPortClr = (PORTreg_t)&GPIO.out_w1tc;
  }

  spi_bus_config_t buscfg = {
      .mosi_io_num = _mosi,
      .miso_io_num = _miso,
      .sclk_io_num = _sck,
      .quadwp_io_num = _quadwp,
      .quadhd_io_num = _quadhd,
      .max_transfer_sz = (SEND_BUF_SIZE * 16) + 8,
      .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS};
  esp_err_t ret = spi_bus_initialize(QSPI_SPI_HOST, &buscfg, QSPI_DMA_CHANNEL);
  if (ret != ESP_OK)
  {
    ESP_ERROR_CHECK(ret);
    return false;
  }

  spi_device_interface_config_t devcfg = {
      .command_bits = 8,
      .address_bits = 24,
      .mode = QSPI_SPI_MODE,
      .clock_speed_hz = QSPI_FREQUENCY,
      .spics_io_num = -1,
      .flags = SPI_DEVICE_HALFDUPLEX,
      .queue_size = 17};
  ret = spi_bus_add_device(QSPI_SPI_HOST, &devcfg, &_handle);
  if (ret != ESP_OK)
  {
    ESP_ERROR_CHECK(ret);
    return false;
  }

  _send_buf = (uint16_t *)heap_caps_malloc(SEND_BUF_SIZE, MALLOC_CAP_INTERNAL);
  if (!_send_buf)
  {
    log_e("_send_buf malloc failed!");
    return false;
  }

  return true;
}

void Arduino_ESP32QSPI::beginWrite()
{
}

void Arduino_ESP32QSPI::endWrite()
{
}

void Arduino_ESP32QSPI::writeCommand(uint8_t c)
{
  CS_LOW();
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.flags = (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR);
  t.cmd = 0x02;
  t.addr = ((uint32_t)c) << 8;
  t.tx_buffer = NULL;
  t.length = 0;
  spi_device_polling_transmit(_handle, &t);
  CS_HIGH();
}

void Arduino_ESP32QSPI::writeCommand16(uint16_t c)
{
  CS_LOW();
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.flags = (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR);
  t.cmd = 0x02;
  t.addr = c;
  t.tx_buffer = NULL;
  t.length = 0;
  spi_device_polling_transmit(_handle, &t);
  CS_HIGH();
}

void Arduino_ESP32QSPI::write(uint8_t d)
{
  CS_LOW();
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.flags = SPI_TRANS_MODE_QIO;
  t.cmd = 0x32;
  t.addr = 0x002C00;
  t.tx_buffer = &d;
  t.length = 8;
  spi_device_polling_transmit(_handle, &t);
  CS_HIGH();
}

void Arduino_ESP32QSPI::write16(uint16_t d)
{
  CS_LOW();
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.flags = SPI_TRANS_MODE_QIO;
  t.cmd = 0x32;
  t.addr = 0x002C00;
  uint8_t buf[] = {d >> 8, d};
  t.tx_buffer = buf;
  t.length = 16;
  spi_device_polling_transmit(_handle, &t);
  CS_HIGH();
}

void Arduino_ESP32QSPI::writeC8D8(uint8_t c, uint8_t d)
{
  CS_LOW();
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.flags = (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR);
  t.cmd = 0x02;
  t.addr = ((uint32_t)c) << 8;
  t.tx_buffer = &d;
  t.length = 8;
  spi_device_polling_transmit(_handle, &t);
  CS_HIGH();
}

void Arduino_ESP32QSPI::writeC8D16D16(uint8_t c, uint16_t d1, uint16_t d2)
{
  CS_LOW();
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.flags = (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR);
  t.cmd = 0x02;
  t.addr = ((uint32_t)c) << 8;
  uint8_t buf[] = {d1 >> 8, d1, d2 >> 8, d2};
  t.tx_buffer = buf;
  t.length = 32;
  spi_device_polling_transmit(_handle, &t);
  CS_HIGH();
}

void Arduino_ESP32QSPI::writeRepeat(uint16_t p, uint32_t len)
{
  MSB_16_SET(p, p);
  bool first_send = 1;
  uint32_t l = (len > (SEND_BUF_SIZE / 2)) ? (SEND_BUF_SIZE / 2) : len;
  for (int i = 0; i < l; ++i)
  {
    _send_buf[i] = p;
  }

  CS_LOW();
  while (len)
  {
    if (len < l)
    {
      l = len;
    }

    spi_transaction_ext_t t;
    memset(&t, 0, sizeof(t));
    if (first_send)
    {
      t.base.flags = SPI_TRANS_MODE_QIO;
      t.base.cmd = 0x32;
      t.base.addr = 0x003C00;
      first_send = 0;
    }
    else
    {
      t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                     SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
      t.command_bits = 0;
      t.address_bits = 0;
    }
    t.base.tx_buffer = _send_buf;
    t.base.length = l * 16;

    spi_device_polling_transmit(_handle, (spi_transaction_t *)&t);
    len -= l;
  }
  CS_HIGH();
}

void Arduino_ESP32QSPI::writePixels(uint16_t *data, uint32_t len)
{
  // log_i("Arduino_ESP32QSPI::writePixels(%d)", len);

  bool first_send = 1;
  uint32_t l = (len > (SEND_BUF_SIZE / 2)) ? (SEND_BUF_SIZE / 2) : len;

  CS_LOW();
  uint16_t p;
  while (len)
  {
    if (len < l)
    {
      l = len;
    }

    spi_transaction_ext_t t;
    memset(&t, 0, sizeof(t));
    if (first_send)
    {
      t.base.flags = SPI_TRANS_MODE_QIO;
      t.base.cmd = 0x32;
      t.base.addr = 0x003C00;
      first_send = 0;
    }
    else
    {
      t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                     SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
      t.command_bits = 0;
      t.address_bits = 0;
    }
    for (uint8_t i = 0; i < l; ++i)
    {
      p = *data++;
      MSB_16_SET(_send_buf[i], p);
    }

    t.base.tx_buffer = _send_buf;
    t.base.length = l * 16;

    spi_device_polling_transmit(_handle, (spi_transaction_t *)&t);
    len -= l;
  }
  CS_HIGH();
}

void Arduino_ESP32QSPI::writeBytes(uint8_t *data, uint32_t len)
{
  bool first_send = 1;
  uint32_t l = (len > (SEND_BUF_SIZE)) ? (SEND_BUF_SIZE) : len;

  CS_LOW();
  while (len)
  {
    if (len < l)
    {
      l = len;
    }

    spi_transaction_ext_t t;
    memset(&t, 0, sizeof(t));
    if (first_send)
    {
      t.base.flags = SPI_TRANS_MODE_QIO;
      t.base.cmd = 0x32;
      t.base.addr = 0x003C00;
      first_send = 0;
    }
    else
    {
      t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                     SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
      t.command_bits = 0;
      t.address_bits = 0;
    }
    // for (uint8_t i = 0; i < l; ++i)
    // {
    //   _send_buf[i] = *data++;
    // }

    t.base.tx_buffer = data;
    t.base.length = l * 8;

    spi_device_polling_transmit(_handle, (spi_transaction_t *)&t);
    len -= l;
    data += l;
  }
  CS_HIGH();
}

void Arduino_ESP32QSPI::writePattern(uint8_t *data, uint8_t len, uint32_t repeat)
{
  while (repeat--)
  {
    writeBytes(data, len);
  }
}

/******** low level bit twiddling **********/

INLINE void Arduino_ESP32QSPI::CS_HIGH(void)
{
  *_csPortSet = _csPinMask;
}

INLINE void Arduino_ESP32QSPI::CS_LOW(void)
{
  *_csPortClr = _csPinMask;
}

#endif // #if defined(ESP32)
