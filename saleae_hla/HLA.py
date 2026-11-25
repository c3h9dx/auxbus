from saleae.analyzers import HighLevelAnalyzer, AnalyzerFrame


class AUXBUSHla(HighLevelAnalyzer):
    """
    Elecraft AUXBUS High-Level Analyzer.
    """

    result_types = {
        'cmd':  {'format': '{{data.source}} {{data.kind}} cmd={{data.cmd}} data={{data.data}}'
                           ' response={{data.response}}  {{data.info}}{{data.warn}}'},
        'freq': {'format': '{{data.source}} FREQ value={{data.value}}  {{data.info}}{{data.warn}}'},
        'amp':  {'format': '{{data.source}} {{data.kind}} byte={{data.byte}}  {{data.info}}{{data.warn}}'},
        'raw':  {'format': '{{data.source}} ? value={{data.value}}  {{data.info}}{{data.warn}}'},
    }

    def __init__(self):
        self._bits        = []
        self._frame_start = None
        self._frame_end   = None
        self._amp         = False
        self._nbits       = 24
        self._warn        = []
        self._tail        = False

    def decode(self, frame: AnalyzerFrame):
        if frame.type == 'preamble':
            self._bits        = []
            self._frame_start = frame.start_time
            self._frame_end   = frame.end_time
            self._amp         = frame.data.get('dir', 'K*') in ('KPA*500', 'AMP')
            self._tail        = bool(frame.data.get('tail'))
            self._nbits       = _u8(frame.data.get('nbits', 24))
            self._warn        = []
            if frame.data.get('collision'):
                self._warn.append('collision')
            if self._tail:
                self._warn.append('response tail after K4FP window, K4FP reads it as an amp frame')
            low = frame.data.get('low_us')
            if not self._amp and low is not None and not (1600 <= int(low) <= 2200):
                self._warn.append('preamble LOW %d us' % int(low))
            return None

        if frame.type == 'bit':
            if self._frame_start is None:
                return None                      # bit bez preambuly (start nagrania)
            self._bits.append(_u8(frame.data['value']))
            self._frame_end = frame.end_time
            if frame.data.get('near_edge'):
                self._warn.append('edge near sample %d' % _u8(frame.data.get('index', 0)))
            if len(self._bits) < self._nbits:
                return None
            bits, start, end = self._bits, self._frame_start, self._frame_end
            self._bits, self._frame_start = [], None
            if self._amp:
                ftype, d = _decode_amp(bits)
                if self._tail:
                    d['source'] = '?'
            else:
                ftype, d = self._decode_radio(bits)
            d['warn'] = ('  ! ' + ', '.join(self._warn)) if self._warn else ''
            result = AnalyzerFrame(ftype, start, end, d)
            print(_format(ftype, d))
            return result

        return None

    # ------------------------------------------------------------------
    def _decode_radio(self, bits):
        bits = (list(bits) + [1] * 24)[:24]
        v24 = _bits_to_int(bits)
        cmd, data, resp = v24 >> 16, (v24 >> 8) & 0xFF, v24 & 0xFF

        if 0x40 <= cmd <= 0x7F:
            return 'freq', {'source': 'K*', 'value': '0x%06X' % v24, 'info': _freq_info(v24)}

        kind = _COMMANDS.get(cmd, 'CMD')
        if cmd == 0x20:
            info = 'band ' + _BAND_BY_VALUE.get(data, '? (%d)' % data)
        elif cmd == 0x21:
            info = 'XVTR off (HF band)' if data == 0 else 'XVTR slot %d' % data
        elif cmd == 0x34:
            info = 'read KRC2 status char'
        elif cmd == 0x35:
            info = 'set KRC2 AC outputs'
        elif cmd == 0x38:
            info = {0: 'next antenna', 1: 'antenna 1', 2: 'antenna 2'}.get(
                data, 'antenna ? (ignored by KPA1500)')
        elif cmd == 0x39:
            info = 'ATU tune (FullSearch)' if data == 0 else 'ATU ? (data != 0, ignored by KPA1500)'
        elif cmd & 0x80:
            info = 'unknown command (bit 23 set, rejected by KPA1500)'
        else:
            info = 'unknown command'
        if resp != 0xFF:
            info += '; K4 stores response: AX; -> AX%02X;' % resp
        return 'cmd', {'source': 'K*', 'kind': kind, 'cmd': '0x%02X' % cmd,
                       'data': '0x%02X' % data, 'response': '0x%02X' % resp, 'info': info}


# ------------------------------------------------------------------
# Tabele i dekodery
# ------------------------------------------------------------------

# FRAME_GENERATOR_SPEC.md §3.2
_COMMANDS = {0x20: 'BAND', 0x21: 'XVTR', 0x34: 'STATUS', 0x35: 'AC', 0x38: 'ANT', 0x39: 'ATU'}

# cmd 0x20: numeracja pasm radio -> wzmacniacz (inna niz CAT BN; §3.3)
_BAND_BY_VALUE = {
    0: '160m', 1: '80m', 2: '40m', 3: '30m', 4: '20m',
    5: '17m',  6: '15m', 7: '12m', 8: '10m', 9: '60m', 10: '6m',
}


def _freq_info(v24):
    """Frequency (§3.4a): V = (kHz << 5) | 0x400000 | cs, suma 6 nibli = 0 mod 16.
    kHz liczone jak w KPA1500: (V & ~0x400000) >> 5."""
    khz = (v24 & ~0x400000) >> 5
    nib_ok = sum((v24 >> s) & 0xF for s in range(0, 24, 4)) % 16 == 0
    zero_ok = not (v24 >> 4) & 1
    info = '%d kHz, checksum %s' % (khz, 'OK' if nib_ok and zero_ok else 'BAD')
    if v24 >> 21 == 0b011:
        info += ', 0b011 prefix (KPA1500 accepts, K4 never sends)'
    return info


_AMP_FAULT = ["powered on", "power-off problem", "current 1", "current 2", "temperature",
              "firmware rev. changed", "power input too high", "frequency counter",
              "60 volt power supply", "SWR too high", "T/R switch failure", "PA failure",
              "PA output too high", "60 V too low", "MON270 signal", "excess gain"]
# numeracja przyciskow pasm wzmacniacz -> radio (§4.5)
_AMP_BAND_BUTTON = {0: '60m', 1: '160m', 2: '80m', 3: '40m', 4: '30m', 5: '20m',
                    6: '17m', 7: '15m', 8: '12m', 9: '10m', 10: '6m'}
_AMP_STATE = {0x31: 'on (KPA500)', 0x32: 'off', 0x33: 'standby', 0x34: 'operate',
              0x37: 'on (KPA1500)'}
_AMP_KEY = {0x35: 'key start, K4 sends TU2', 0x36: 'key stop, K4 sends TU0'}


def _decode_amp(bits):
    """7 bitow b6..b0 tak jak probkuje K4FP; b6 = OPER (FRAME_GENERATOR_SPEC §4.4)."""
    b = _bits_to_int(bits[:7])
    x = b & 0x3F
    if x <= 0x0A:
        kind, info = 'BAND', 'band button ' + _AMP_BAND_BUTTON[x]
    elif 0x20 <= x <= 0x2F:
        kind, info = 'FAULT', 'fault %d: %s' % (x & 15, _AMP_FAULT[x & 15])
    elif x in _AMP_STATE:
        kind, info = 'STATE', _AMP_STATE[x]
    elif x in _AMP_KEY:
        kind, info = 'KEY', _AMP_KEY[x]
    else:
        kind, info = '?', 'unknown, ignored by K4'
    if b & 0x40:
        info += ', OPER'                       # bit 6 = wzmacniacz w OPER (0x74 = operate)
    return 'amp', {'source': 'KPA*500', 'kind': kind, 'byte': '0x%02X' % b, 'info': info}


def _bits_to_int(bit_list):
    v = 0
    for b in bit_list:
        v = (v << 1) | int(b)
    return v


def _u8(raw):
    return raw[0] if isinstance(raw, (bytes, bytearray)) else int(raw)


def _format(ftype, d):
    fmt = AUXBUSHla.result_types[ftype]['format']
    for k, v in d.items():
        fmt = fmt.replace('{{data.%s}}' % k, str(v))
    return fmt
