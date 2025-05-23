/*
 * This file is part of the Trezor project, https://trezor.io/
 *
 * Copyright (C) 2014 Pavol Rusnak <stick@satoshilabs.com>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bignum.h"
#include "bitmaps.h"
#include "config.h"
#include "crypto.h"
#include "gettext.h"
#include "layout2.h"
#include "memzero.h"
#include "nem2.h"
#include "oled.h"
#include "qrcodegen.h"
#include "secp256k1.h"
#include "string.h"
#include "timer.h"
#include "util.h"

#define LOCKTIME_TIMESTAMP_MIN_VALUE 500000000

#if !BITCOIN_ONLY

static const char *slip44_extras(uint32_t coin_type) {
  if ((coin_type & PATH_HARDENED) == 0) {
    return 0;
  }
  switch (coin_type & PATH_UNHARDEN_MASK) {
    case 40:
      return "EXP";  // Expanse
    case 43:
      return "NEM";  // NEM
    case 60:
      return "ETH";  // Ethereum Mainnet
    case 61:
      return "ETC";  // Ethereum Classic Mainnet
    case 108:
      return "UBQ";  // UBIQ
    case 137:
      return "RSK";  // Rootstock Mainnet
    case 37310:
      return "tRSK";  // Rootstock Testnet
  }
  return 0;
}

#endif

static const char *address_n_str(const uint32_t *address_n,
                                 size_t address_n_count,
                                 bool address_is_account) {
  if (address_n_count > 8) {
    return _("Unknown long path");
  }
  if (address_n_count == 0) {
    return _("Path: m");
  }

  enum {
    ACCOUNT_NONE,
    ACCOUNT_BIP44,
    ACCOUNT_BIP49,
    ACCOUNT_BIP84,
    ACCOUNT_BIP86,
    ACCOUNT_SLIP25
  } account_type = ACCOUNT_NONE;

  if ((address_n[1] & PATH_HARDENED) && (address_n[2] & PATH_HARDENED) &&
      (address_n[address_n_count - 2] <= PATH_MAX_CHANGE) &&
      (address_n[address_n_count - 1] <= PATH_MAX_ADDRESS_INDEX)) {
    if (address_n_count == 5 && address_n[0] == PATH_HARDENED + 44) {
      account_type = ACCOUNT_BIP44;
    } else if (address_n_count == 5 && address_n[0] == PATH_HARDENED + 49) {
      account_type = ACCOUNT_BIP49;
    } else if (address_n_count == 5 && address_n[0] == PATH_HARDENED + 84) {
      account_type = ACCOUNT_BIP84;
    } else if (address_n_count == 5 && address_n[0] == PATH_HARDENED + 86) {
      account_type = ACCOUNT_BIP86;
    } else if (address_n_count == 6 && address_n[0] == PATH_SLIP25_PURPOSE &&
               (address_n[3] & PATH_HARDENED)) {
      account_type = ACCOUNT_SLIP25;
    }
  }

  // known BIP44/49/84/86 path
  static char path[100];
  if (account_type != ACCOUNT_NONE) {
    bool legacy = false;
    const CoinInfo *coin = coinBySlip44(address_n[1]);
    const char *abbr = 0;
    if (account_type == ACCOUNT_BIP86 || account_type == ACCOUNT_SLIP25) {
      if (coin && coin->has_taproot && coin->bech32_prefix) {
        abbr = coin->coin_shortcut;
      }
    } else if (account_type == ACCOUNT_BIP84) {
      if (coin && coin->has_segwit && coin->bech32_prefix) {
        abbr = coin->coin_shortcut;
      }
    } else if (account_type == ACCOUNT_BIP49) {
      if (coin && coin->has_segwit) {
        abbr = coin->coin_shortcut;
      }
    } else {
      if (coin) {
        if (coin->has_segwit) {
          legacy = true;
        }
        abbr = coin->coin_shortcut;
#if !BITCOIN_ONLY
      } else {
        abbr = slip44_extras(address_n[1]);
#endif
      }
    }
    const uint32_t accnum = address_is_account
                                ? ((address_n[4] & PATH_UNHARDEN_MASK) + 1)
                                : (address_n[2] & PATH_UNHARDEN_MASK) + 1;
    if (abbr && accnum < 100) {
      memzero(path, sizeof(path));
      strlcpy(path, abbr, sizeof(path));
      // Account naming:
      // "Legacy", "Legacy SegWit", "SegWit", "Taproot" and "Coinjoin" for
      // BIP44/P2PKH, BIP49/P2SH-P2WPKH, BIP84/P2WPKH, BIP86/P2TR, SLIP25/P2TR
      // respectively. For non-segwit coins we use only BIP44 with no special
      // naming.
      if (legacy) {
        strlcat(path, " Legacy", sizeof(path));
      } else if (account_type == ACCOUNT_BIP49) {
        strlcat(path, " L.SegWit", sizeof(path));
      } else if (account_type == ACCOUNT_BIP84) {
        strlcat(path, " SegWit", sizeof(path));
      } else if (account_type == ACCOUNT_BIP86) {
        strlcat(path, " Taproot", sizeof(path));
      } else if (account_type == ACCOUNT_SLIP25) {
        strlcat(path, " Coinjoin", sizeof(path));
      }

      if (address_is_account) {
        strlcat(path, " address", sizeof(path));
      } else {
        strlcat(path, " account", sizeof(path));
      }

      if (!(account_type == ACCOUNT_SLIP25 && accnum == 1)) {
        char acc[5] = {' ', '#'};
        if (accnum < 10) {
          acc[2] = '0' + accnum;
          acc[3] = '\0';
        } else {
          acc[2] = '0' + (accnum / 10);
          acc[3] = '0' + (accnum % 10);
          acc[4] = '\0';
        }
        strlcat(path, acc, sizeof(path));
      }
      return path;
    }
  }

  //                  "Path: m"    /    i   '
  static char address_str[7 + 8 * (1 + 10 + 1) + 1];
  char *c = address_str + sizeof(address_str) - 1;

  *c = 0;
  c--;

  for (int n = (int)address_n_count - 1; n >= 0; n--) {
    uint32_t i = address_n[n];
    if (i & PATH_HARDENED) {
      *c = '\'';
      c--;
    }
    i = i & PATH_UNHARDEN_MASK;
    do {
      *c = '0' + (i % 10);
      c--;
      i /= 10;
    } while (i > 0);
    *c = '/';
    c--;
  }
  *c = 'm';
  c--;
  *c = ' ';
  c--;
  *c = ':';
  c--;
  *c = 'h';
  c--;
  *c = 't';
  c--;
  *c = 'a';
  c--;
  *c = 'P';

  return c;
}

// split longer string into 4 rows, rowlen chars each
const char **split_message(const uint8_t *msg, uint32_t len, uint32_t rowlen) {
  static char str[4][32 + 1];
  if (rowlen > 32) {
    rowlen = 32;
  }

  memzero(str, sizeof(str));
  for (int i = 0; i < 4; ++i) {
    size_t show_len = strnlen((char *)msg, MIN(rowlen, len));
    memcpy(str[i], (char *)msg, show_len);
    str[i][show_len] = '\0';
    msg += show_len;
    len -= show_len;
  }

  if (len > 0) {
    str[3][rowlen - 1] = '.';
    str[3][rowlen - 2] = '.';
    str[3][rowlen - 3] = '.';
  }
  static const char *ret[4] = {str[0], str[1], str[2], str[3]};
  return ret;
}

const char **split_message_hex(const uint8_t *msg, uint32_t len) {
  char hex[32 * 2 + 1] = {0};
  memzero(hex, sizeof(hex));
  uint32_t size = len;
  if (len > 32) {
    size = 32;
  }
  data2hex(msg, size, hex);
  if (len > 32) {
    hex[63] = '.';
    hex[62] = '.';
  }
  return split_message((const uint8_t *)hex, size * 2, 16);
}

void *layoutLast = NULL;

void layoutDialogSwipeWrapping(const BITMAP *icon, const char *btnNo,
                               const char *btnYes, const char *heading,
                               const char *description, const char *wrap_text) {
  const uint32_t row_len = 18;
  const char **str =
      split_message((const uint8_t *)wrap_text, strlen(wrap_text), row_len);
  layoutDialogSwipe(icon, btnNo, btnYes, NULL, heading, description, str[0],
                    str[1], str[2], str[3]);
}

void layoutDialogSwipe(const BITMAP *icon, const char *btnNo,
                       const char *btnYes, const char *desc, const char *line1,
                       const char *line2, const char *line3, const char *line4,
                       const char *line5, const char *line6) {
  layoutDialogSwipeEx(icon, btnNo, btnYes, desc, line1, line2, line3, line4,
                      line5, line6, FONT_STANDARD);
}

void layoutDialogSwipeEx(const BITMAP *icon, const char *btnNo,
                         const char *btnYes, const char *desc,
                         const char *line1, const char *line2,
                         const char *line3, const char *line4,
                         const char *line5, const char *line6, uint8_t font) {
  layoutLast = layoutDialogSwipe;
  layoutSwipe();
  layoutDialogEx(icon, btnNo, btnYes, desc, line1, line2, line3, line4, line5,
                 line6, font);
}

void layoutProgressSwipe(const char *desc, int permil) {
  if (layoutLast == layoutProgressSwipe) {
    oledClear();
  } else {
    layoutLast = layoutProgressSwipe;
    layoutSwipe();
  }
  layoutProgress(desc, permil);
}

void layoutScreensaver(void) {
  if (system_millis_busy_deadline > timer_ms()) {
    // Busy screen overrides the screensaver.
    layoutBusyscreen();
  } else {
    layoutLast = layoutScreensaver;
    oledClear();
    oledRefresh();
  }
}

void layoutHome(void) {
  if (layoutLast != layoutHomescreen && layoutLast != layoutBusyscreen) {
    // Reset lock screen timeout
    system_millis_lock_start = timer_ms();
  }

  if (system_millis_busy_deadline > timer_ms()) {
    layoutBusyscreen();
  } else {
    layoutHomescreen();
  }
}

void layoutHomescreen(void) {
  if (layoutLast == layoutHomescreen || layoutLast == layoutScreensaver) {
    oledClear();
  } else {
    layoutSwipe();
  }
  layoutLast = layoutHomescreen;

  char label[MAX_LABEL_LEN + 1] = _("Go to trezor.io/start");
  if (config_isInitialized()) {
    config_getLabel(label, sizeof(label));
  }

  uint8_t homescreen[HOMESCREEN_SIZE] = {0};
  if (config_getHomescreen(homescreen, sizeof(homescreen))) {
    BITMAP b = {0};
    b.width = 128;
    b.height = 64;
    b.data = homescreen;
    oledDrawBitmap(0, 0, &b);
  } else {
    if (label[0] != '\0') {
      oledDrawBitmap(44, 4, &bmp_logo48);
      oledDrawStringCenter(OLED_WIDTH / 2, OLED_HEIGHT - 8, label,
                           FONT_STANDARD);
    } else {
      oledDrawBitmap(40, 0, &bmp_logo64_half);
      oledDrawBitmapFlip(40 + 24, 0, &bmp_logo64_half);
    }
  }

  bool no_backup = false;
  bool unfinished_backup = false;
  bool needs_backup = false;
  config_getNoBackup(&no_backup);
  config_getUnfinishedBackup(&unfinished_backup);
  config_getNeedsBackup(&needs_backup);
  if (no_backup) {
    oledBox(0, 0, 127, 8, false);
    oledDrawStringCenter(OLED_WIDTH / 2, 0, "SEEDLESS", FONT_STANDARD);
  } else if (unfinished_backup) {
    oledBox(0, 0, 127, 8, false);
    oledDrawStringCenter(OLED_WIDTH / 2, 0, "BACKUP FAILED!", FONT_STANDARD);
  } else if (needs_backup) {
    oledBox(0, 0, 127, 8, false);
    oledDrawStringCenter(OLED_WIDTH / 2, 0, "NEEDS BACKUP!", FONT_STANDARD);
  }
  oledRefresh();
}

void layoutBusyscreen(void) {
  if (layoutLast == layoutBusyscreen || layoutLast == layoutScreensaver) {
    oledClear();
  } else {
    layoutSwipe();
  }
  layoutLast = layoutBusyscreen;

  layoutDialog(&bmp_icon_warning, NULL, NULL, NULL, _("Please wait"), NULL,
               _("Coinjoin in progress."), NULL, _("Do not disconnect"),
               _("your Trezor."));
}

static void render_address_dialog(const CoinInfo *coin, const char *address,
                                  const char *line1, const char *line2,
                                  const char *extra_line) {
  if (coin && coin->cashaddr_prefix) {
    /* If this is a cashaddr address, remove the prefix from the
     * string presented to the user
     */
    int prefix_len = strlen(coin->cashaddr_prefix);
    if (strncmp(address, coin->cashaddr_prefix, prefix_len) == 0 &&
        address[prefix_len] == ':') {
      address += prefix_len + 1;
    }
  }
  int addrlen = strlen(address);
  int numlines = addrlen <= 42 ? 2 : 3;
  int linelen = (addrlen - 1) / numlines + 1;
  if (linelen > 21) {
    linelen = 21;
  }
  const char **str = split_message((const uint8_t *)address, addrlen, linelen);
  layoutLast = layoutDialogSwipe;
  layoutSwipe();
  oledClear();
  oledDrawBitmap(0, 0, &bmp_icon_question);
  oledDrawString(20, 0 * 9, line1, FONT_STANDARD);
  oledDrawString(20, 1 * 9, line2, FONT_STANDARD);
  int left = linelen > 18 ? 0 : 20;
  oledDrawString(left, 2 * 9, str[0], FONT_FIXED);
  oledDrawString(left, 3 * 9, str[1], FONT_FIXED);
  oledDrawString(left, 4 * 9, str[2], FONT_FIXED);
  oledDrawString(left, 5 * 9, str[3], FONT_FIXED);
  if (!str[3][0]) {
    if (extra_line) {
      oledDrawString(0, 5 * 9, extra_line, FONT_STANDARD);
    } else {
      oledHLine(OLED_HEIGHT - 13);
    }
  }
  layoutButtonNo(_("Cancel"), &bmp_btn_cancel);
  layoutButtonYes(_("Confirm"), &bmp_btn_confirm);
  oledRefresh();
}

static size_t format_coin_amount(uint64_t amount, const char *prefix,
                                 const CoinInfo *coin, AmountUnit amount_unit,
                                 char *output, size_t output_len) {
  // " " + (optional "m"/u") + shortcut + ending zero -> 16 should suffice
  char suffix[16];
  memzero(suffix, sizeof(suffix));
  suffix[0] = ' ';
  uint32_t decimals = coin->decimals;
  switch (amount_unit) {
    case AmountUnit_SATOSHI:
      decimals = 0;
      strlcpy(suffix + 1, "sat", sizeof(suffix) - 1);
      if (strcmp(coin->coin_shortcut, "BTC") != 0) {
        strlcpy(suffix + 4, " ", sizeof(suffix) - 4);
        strlcpy(suffix + 5, coin->coin_shortcut, sizeof(suffix) - 5);
      }
      break;
    case AmountUnit_MILLIBITCOIN:
      if (decimals >= 6) {
        decimals -= 6;
        suffix[1] = 'u';
        strlcpy(suffix + 2, coin->coin_shortcut, sizeof(suffix) - 2);
      } else {
        strlcpy(suffix + 1, coin->coin_shortcut, sizeof(suffix) - 1);
      }
      break;
    case AmountUnit_MICROBITCOIN:
      if (decimals >= 3) {
        decimals -= 3;
        suffix[1] = 'm';
        strlcpy(suffix + 2, coin->coin_shortcut, sizeof(suffix) - 2);
      } else {
        strlcpy(suffix + 1, coin->coin_shortcut, sizeof(suffix) - 1);
      }
      break;
    default:  // AmountUnit_BITCOIN
      strlcpy(suffix + 1, coin->coin_shortcut, sizeof(suffix) - 1);
      break;
  }
  return bn_format_amount(amount, prefix, suffix, decimals, output, output_len);
}

void layoutConfirmOutput(const CoinInfo *coin, AmountUnit amount_unit,
                         const TxOutputType *out) {
  char str_out[32 + 3] = {0};
  format_coin_amount(out->amount, NULL, coin, amount_unit, str_out,
                     sizeof(str_out) - 3);
  strlcat(str_out, " to", sizeof(str_out));
  const char *address = out->address;
  const char *extra_line =
      (out->address_n_count > 0)
          ? address_n_str(out->address_n, out->address_n_count, false)
          : 0;
  render_address_dialog(coin, address, _("Confirm sending"), str_out,
                        extra_line);
}

void layoutConfirmOmni(const uint8_t *data, uint32_t size) {
  const char *desc = NULL;
  char str_out[32] = {0};
  uint32_t tx_type = 0, currency = 0;
  REVERSE32(*(const uint32_t *)(data + 4), tx_type);
  if (tx_type == 0x00000000 && size == 20) {  // OMNI simple send
    desc = _("Simple send of ");
    REVERSE32(*(const uint32_t *)(data + 8), currency);
    const char *suffix = " UNKN";
    bool divisible = false;
    switch (currency) {
      case 1:
        suffix = " OMNI";
        divisible = true;
        break;
      case 2:
        suffix = " tOMNI";
        divisible = true;
        break;
      case 3:
        suffix = " MAID";
        divisible = false;
        break;
      case 31:
        suffix = " USDT";
        divisible = true;
        break;
    }
    uint64_t amount_be = 0, amount = 0;
    memcpy(&amount_be, data + 12, sizeof(uint64_t));
    REVERSE64(amount_be, amount);
    bn_format_amount(amount, NULL, suffix, divisible ? 8 : 0, str_out,
                     sizeof(str_out));
  } else {
    desc = _("Unknown transaction");
    str_out[0] = 0;
  }
  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                    _("Confirm OMNI Transaction:"), NULL, desc, NULL, str_out,
                    NULL);
}

bool is_valid_ascii(const uint8_t *data, uint32_t size) {
  for (uint32_t i = 0; i < size; i++) {
    if (data[i] < ' ' || data[i] > '~') {
      return false;
    }
  }
  return true;
}

void layoutConfirmOpReturn(const uint8_t *data, uint32_t size) {
  const char **str = NULL;
  if (!is_valid_ascii(data, size)) {
    str = split_message_hex(data, size);
  } else {
    str = split_message(data, size, 20);
  }
  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                    _("Confirm OP_RETURN:"), str[0], str[1], str[2], str[3],
                    NULL);
}

static bool formatAmountDifference(const CoinInfo *coin, AmountUnit amount_unit,
                                   uint64_t amount1, uint64_t amount2,
                                   char *output, size_t output_length) {
  uint64_t abs_diff = 0;
  const char *sign = NULL;
  if (amount1 >= amount2) {
    abs_diff = amount1 - amount2;
  } else {
    abs_diff = amount2 - amount1;
    sign = "-";
  }

  return format_coin_amount(abs_diff, sign, coin, amount_unit, output,
                            output_length) != 0;
}

// Computes numer / denom and rounds to the nearest integer.
static uint64_t div_round(uint64_t numer, uint64_t denom) {
  return numer / denom + (2 * (numer % denom) >= denom);
}

static bool formatComputedFeeRate(uint64_t fee, uint64_t tx_weight,
                                  char *output, size_t output_length,
                                  bool segwit, bool parentheses) {
  // Convert transaction weight to virtual transaction size, which is defined
  // as tx_weight / 4 rounded up to the next integer.
  // https://github.com/bitcoin/bips/blob/master/bip-0141.mediawiki#transaction-size-calculations
  uint64_t tx_size = (tx_weight + 3) / 4;

  // Compute fee rate and modify it in place for the bn_format_amount()
  // function. We multiply by 100, because we want bn_format_amount() to display
  // two decimal digits.
  uint64_t fee_rate_multiplied = div_round(100 * fee, tx_size);

  size_t length =
      bn_format_amount(fee_rate_multiplied, parentheses ? "(" : NULL,
                       segwit ? " sat/vB" : " sat/B", 2, output, output_length);
  if (length == 0) {
    return false;
  }

  if (parentheses) {
    if (length + 2 > output_length) {
      return false;
    }
    output[length] = ')';
    output[length + 1] = '\0';
  }
  return true;
}

static bool formatFeeRate(uint64_t fee_per_kvbyte, char *output,
                          size_t output_length, bool segwit) {
  return formatComputedFeeRate(fee_per_kvbyte, 4000, output, output_length,
                               segwit, false);
}

void layoutConfirmTx(const CoinInfo *coin, AmountUnit amount_unit,
                     uint64_t total_in, uint64_t external_in,
                     uint64_t total_out, uint64_t change_out,
                     uint64_t tx_weight) {
  char str_out[32] = {0};
  formatAmountDifference(coin, amount_unit, total_in, change_out, str_out,
                         sizeof(str_out));

  if (external_in == 0) {
    char str_fee[32] = {0};
    formatAmountDifference(coin, amount_unit, total_in, total_out, str_fee,
                           sizeof(str_fee));

    char str_fee_rate[32] = {0};
    bool show_fee_rate = total_in >= total_out;

    if (show_fee_rate) {
      formatComputedFeeRate(total_in - total_out, tx_weight, str_fee_rate,
                            sizeof(str_fee_rate), coin->has_segwit, true);
    }

    layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                      _("Confirm sending:"), str_out, _("including fee:"),
                      str_fee, show_fee_rate ? str_fee_rate : NULL, NULL);
  } else {
    char str_spend[32] = {0};
    formatAmountDifference(coin, amount_unit, total_in - external_in,
                           change_out, str_spend, sizeof(str_spend));
    layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                      _("You are contributing:"), str_spend,
                      _("to the total amount:"), str_out, NULL, NULL);
  }
}

void layoutConfirmReplacement(const char *description, uint8_t txid[32]) {
  const char **str = split_message_hex(txid, 32);
  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                    description, str[0], str[1], str[2], str[3], NULL);
}

void layoutConfirmModifyOutput(const CoinInfo *coin, AmountUnit amount_unit,
                               TxOutputType *out, TxOutputType *orig_out,
                               int page) {
  if (page == 0) {
    render_address_dialog(coin, out->address, _("Modify amount for"),
                          _("address:"), NULL);
  } else {
    char *question = NULL;
    uint64_t amount_change = 0;
    if (orig_out->amount < out->amount) {
      question = _("Increase amount by:");
      amount_change = out->amount - orig_out->amount;
    } else {
      question = _("Decrease amount by:");
      amount_change = orig_out->amount - out->amount;
    }

    char str_amount_change[32] = {0};
    format_coin_amount(amount_change, NULL, coin, amount_unit,
                       str_amount_change, sizeof(str_amount_change));

    char str_amount_new[32] = {0};
    format_coin_amount(out->amount, NULL, coin, amount_unit, str_amount_new,
                       sizeof(str_amount_new));

    layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                      question, str_amount_change, NULL, _("New amount:"),
                      str_amount_new, NULL);
  }
}

void layoutConfirmModifyFee(const CoinInfo *coin, AmountUnit amount_unit,
                            uint64_t fee_old, uint64_t fee_new,
                            uint64_t tx_weight) {
  char str_fee_change[32] = {0};
  char str_fee_new[32] = {0};
  char *question = NULL;

  uint64_t fee_change = 0;
  if (fee_old < fee_new) {
    question = _("Increase your fee by:");
    fee_change = fee_new - fee_old;
  } else {
    question = _("Decrease your fee by:");
    fee_change = fee_old - fee_new;
  }
  format_coin_amount(fee_change, NULL, coin, amount_unit, str_fee_change,
                     sizeof(str_fee_change));

  format_coin_amount(fee_new, NULL, coin, amount_unit, str_fee_new,
                     sizeof(str_fee_new));

  char str_fee_rate[32] = {0};

  formatComputedFeeRate(fee_new, tx_weight, str_fee_rate, sizeof(str_fee_rate),
                        coin->has_segwit, true);

  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                    question, str_fee_change, _("Transaction fee:"),
                    str_fee_new, str_fee_rate, NULL);
}

void layoutFeeOverThreshold(const CoinInfo *coin, AmountUnit amount_unit,
                            uint64_t fee) {
  char str_fee[32] = {0};
  format_coin_amount(fee, NULL, coin, amount_unit, str_fee, sizeof(str_fee));
  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                    _("Fee"), str_fee, _("is unexpectedly high."), NULL,
                    _("Send anyway?"), NULL);
}

void layoutFeeRateOverThreshold(const CoinInfo *coin, uint32_t fee_per_kvbyte) {
  char str_fee_rate[32] = {0};
  formatFeeRate(fee_per_kvbyte, str_fee_rate, sizeof(str_fee_rate),
                coin->has_segwit);
  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                    _("Fee rate"), str_fee_rate, _("is unexpectedly high."),
                    NULL, _("Proceed anyway?"), NULL);
}

void layoutChangeCountOverThreshold(uint32_t change_count) {
  char str_change[21] = {0};
  snprintf(str_change, sizeof(str_change), "There are %" PRIu32, change_count);
  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                    _("Warning!"), str_change, _("change-outputs."), NULL,
                    _("Continue?"), NULL);
}

void layoutConfirmUnverifiedExternalInputs(void) {
  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                    _("Warning!"), _("The transaction"),
                    _("contains unverified"), _("external inputs."),
                    _("Continue?"), NULL);
}

void layoutConfirmNondefaultLockTime(uint32_t lock_time,
                                     bool lock_time_disabled) {
  if (lock_time_disabled) {
    layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                      _("Warning!"), _("Locktime is set but"),
                      _("will have no effect."), NULL, _("Continue?"), NULL);

  } else {
    char str_locktime[20] = {0};
    char *str_type = NULL;
    if (lock_time < LOCKTIME_TIMESTAMP_MIN_VALUE) {
      str_type = "blockheight:";
      snprintf(str_locktime, sizeof(str_locktime), "%" PRIu32, lock_time);
    } else {
      str_type = "timestamp (UTC):";
      time_t time = lock_time;
      const struct tm *tm = gmtime(&time);
      strftime(str_locktime, sizeof(str_locktime), "%F %T", tm);
    }

    layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                      _("Locktime for this"), _("transaction is set to"),
                      str_type, str_locktime, _("Continue?"), NULL);
  }
}

void layoutAuthorizeCoinJoin(const CoinInfo *coin, uint64_t max_rounds,
                             uint32_t max_fee_per_kvbyte) {
  char str_max_rounds[32] = {0};
  char str_fee_rate[32] = {0};
  bn_format_amount(max_rounds, NULL, NULL, 0, str_max_rounds,
                   sizeof(str_max_rounds));
  formatFeeRate(max_fee_per_kvbyte, str_fee_rate, sizeof(str_fee_rate),
                coin->has_segwit);
  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"),
                    _("Authorize coinjoin"), _("Maximum rounds:"),
                    str_max_rounds, _("Maximum mining fee:"), str_fee_rate,
                    NULL, NULL);
}

void layoutConfirmCoinjoinAccess(void) {
  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                    _("Do you want to allow"), _("access to your"),
                    _("coinjoin account?"), NULL, NULL, NULL);
}

void layoutVerifyAddress(const CoinInfo *coin, const char *address) {
  render_address_dialog(coin, address, _("Confirm address?"),
                        _("Message signed by:"), 0);
}

void layoutCipherKeyValue(bool encrypt, const char *key) {
  const char **str = split_message((const uint8_t *)key, strlen(key), 16);
  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"),
                    encrypt ? _("Encrypt value of this key?")
                            : _("Decrypt value of this key?"),
                    str[0], str[1], str[2], str[3], NULL, NULL);
}

void layoutEncryptMessage(const uint8_t *msg, uint32_t len, bool signing) {
  const char **str = split_message(msg, len, 16);
  layoutDialogSwipe(
      &bmp_icon_question, _("Cancel"), _("Confirm"),
      signing ? _("Encrypt+Sign message?") : _("Encrypt message?"), str[0],
      str[1], str[2], str[3], NULL, NULL);
}

void layoutDecryptMessage(const uint8_t *msg, uint32_t len,
                          const char *address) {
  const char **str = split_message(msg, len, 16);
  layoutDialogSwipe(
      &bmp_icon_info, NULL, _("OK"),
      address ? _("Decrypted signed message") : _("Decrypted message"), str[0],
      str[1], str[2], str[3], NULL, NULL);
}

void layoutResetWord(const char *word, int pass, int word_pos, bool last) {
  layoutLast = layoutResetWord;
  layoutSwipe();

  const char *btnYes = NULL;
  if (last) {
    if (pass == 1) {
      btnYes = _("Finish");
    } else {
      btnYes = _("Again");
    }
  } else {
    btnYes = _("Next");
  }

  const char *action = NULL;
  if (pass == 1) {
    action = _("Please check the seed");
  } else {
    action = _("Write down the seed");
  }

  char index_str[] = "##th word is:";
  if (word_pos < 10) {
    index_str[0] = ' ';
  } else {
    index_str[0] = '0' + word_pos / 10;
  }
  index_str[1] = '0' + word_pos % 10;
  if (word_pos == 1 || word_pos == 21) {
    index_str[2] = 's';
    index_str[3] = 't';
  } else if (word_pos == 2 || word_pos == 22) {
    index_str[2] = 'n';
    index_str[3] = 'd';
  } else if (word_pos == 3 || word_pos == 23) {
    index_str[2] = 'r';
    index_str[3] = 'd';
  }

  int left = 0;
  oledClear();
  oledDrawBitmap(0, 0, &bmp_icon_info);
  left = bmp_icon_info.width + 4;

  oledDrawString(left, 0 * 9, action, FONT_STANDARD);
  oledDrawString(left, 2 * 9, word_pos < 10 ? index_str + 1 : index_str,
                 FONT_STANDARD);
  oledDrawStringCenter(OLED_WIDTH / 2, 4 * 9 - 3, word,
                       FONT_FIXED | FONT_DOUBLE);
  // 30 is the maximum pixels used for a pixel row in the BIP39 word "abstract"
  oledSCA(4 * 9 - 3 - 2, 4 * 9 - 3 + 15 + 2, 30);
  oledInvert(0, 4 * 9 - 3 - 2, OLED_WIDTH - 1, 4 * 9 - 3 + 15 + 2);
  layoutButtonYes(btnYes, &bmp_btn_confirm);
  oledRefresh();
}

#define QR_MAX_VERSION 9

static void renderQR(const char *text) {
  uint8_t codedata[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)] = {0};
  uint8_t tempdata[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)] = {0};

  int side = 0;
  if (qrcodegen_encodeText(text, tempdata, codedata, qrcodegen_Ecc_LOW,
                           qrcodegen_VERSION_MIN, QR_MAX_VERSION,
                           qrcodegen_Mask_AUTO, true)) {
    side = qrcodegen_getSize(codedata);
  }

  oledInvert(0, 0, 63, 63);
  if (side > 0 && side <= 29) {
    int offset = 32 - side;
    for (int i = 0; i < side; i++) {
      for (int j = 0; j < side; j++) {
        if (qrcodegen_getModule(codedata, i, j)) {
          oledBox(offset + i * 2, offset + j * 2, offset + 1 + i * 2,
                  offset + 1 + j * 2, false);
        }
      }
    }
  } else if (side > 0 && side <= 60) {
    int offset = 32 - (side / 2);
    for (int i = 0; i < side; i++) {
      for (int j = 0; j < side; j++) {
        if (qrcodegen_getModule(codedata, i, j)) {
          oledClearPixel(offset + i, offset + j);
        }
      }
    }
  }
}

void layoutAddress(const char *address, const char *desc, bool qrcode,
                   bool ignorecase, const uint32_t *address_n,
                   size_t address_n_count, bool address_is_account) {
  if (layoutLast != layoutAddress && layoutLast != layoutXPUBMultisig) {
    layoutSwipe();
  } else {
    oledClear();
  }
  layoutLast = layoutAddress;

  uint32_t addrlen = strlen(address);
  if (qrcode) {
    char address_upcase[addrlen + 1];
    memset(address_upcase, 0, sizeof(address_upcase));
    if (ignorecase) {
      for (uint32_t i = 0; i < addrlen + 1; i++) {
        address_upcase[i] = address[i] >= 'a' && address[i] <= 'z'
                                ? address[i] + 'A' - 'a'
                                : address[i];
      }
    }
    renderQR(ignorecase ? address_upcase : address);
  } else {
    if (desc) {
      oledDrawString(0, 0 * 9, desc, FONT_STANDARD);
    }
    if (addrlen > 10) {  // don't split short addresses
      uint32_t rowcount = 4;
      if (addrlen <= 42) {
        rowcount = 2;
      } else if (addrlen <= 63) {
        rowcount = 3;
      }

      uint32_t rowlen = (addrlen - 1) / rowcount + 1;
      const char **str =
          split_message((const uint8_t *)address, addrlen, rowlen);
      for (int i = 0; i < 4; i++) {
        oledDrawString(0, (i + 1) * 9 + 4, str[i], FONT_FIXED);
      }
    } else {
      oledDrawString(0, (0 + 1) * 9 + 4, address, FONT_FIXED);
    }
    oledDrawString(
        0, 42, address_n_str(address_n, address_n_count, address_is_account),
        FONT_STANDARD);
  }

  if (!qrcode) {
    layoutButtonNo(_("QR Code"), NULL);
  }

  layoutButtonYes(_("Confirm"), &bmp_btn_confirm);
  oledRefresh();
}

void layoutPublicKey(const uint8_t *pubkey) {
  char desc[] = "Public Key: 00";
  data2hex(pubkey, 1, desc + 12);
  const char **str = split_message_hex(pubkey + 1, 32 * 2);
  layoutDialogSwipe(&bmp_icon_question, NULL, _("Continue"), NULL, desc, str[0],
                    str[1], str[2], str[3], NULL);
}

static void _layout_xpub(const char *xpub, const char *desc, int page) {
  // 21 characters per line, 4 lines, minus 3 chars for "..." = 81
  // skip 81 characters per page
  xpub += page * 81;
  const char **str = split_message((const uint8_t *)xpub, strlen(xpub), 21);
  oledDrawString(0, 0 * 9, desc, FONT_STANDARD);
  for (int i = 0; i < 4; i++) {
    oledDrawString(0, (i + 1) * 9 + 4, str[i], FONT_FIXED);
  }
}

void layoutXPUB(const char *xpub, int page, bool qrcode) {
  if (layoutLast != layoutAddress && layoutLast != layoutXPUB) {
    layoutSwipe();
  } else {
    oledClear();
  }
  layoutLast = layoutXPUB;
  if (qrcode) {
    renderQR(xpub);
  } else {
    char desc[] = "XPUB _/2";
    desc[5] = '1' + page;
    _layout_xpub(xpub, desc, page);
    layoutButtonNo(_("QR Code"), NULL);
  }
  layoutButtonYes(_("Confirm"), &bmp_btn_confirm);
  oledRefresh();
}

void layoutXPUBMultisig(const char *xpub, int index, int page, bool ours) {
  if (layoutLast != layoutAddress && layoutLast != layoutXPUBMultisig) {
    layoutSwipe();
  } else {
    oledClear();
  }
  layoutLast = layoutXPUBMultisig;
  char desc[] = "XPUB #__ _/2 (________)";
  if (index + 1 >= 10) {
    desc[6] = '0' + (((index + 1) / 10) % 10);
    desc[7] = '0' + ((index + 1) % 10);
  } else {
    desc[6] = '0' + ((index + 1) % 10);
    desc[7] = ' ';
  }
  desc[9] = '1' + page;
  if (ours) {
    desc[14] = 'y';
    desc[15] = 'o';
    desc[16] = 'u';
    desc[17] = 'r';
    desc[18] = 's';
    desc[19] = ')';
    desc[20] = 0;
  } else {
    desc[14] = 'c';
    desc[15] = 'o';
    desc[16] = 's';
    desc[17] = 'i';
    desc[18] = 'g';
    desc[19] = 'n';
    desc[20] = 'e';
    desc[21] = 'r';
  }
  _layout_xpub(xpub, desc, page);
  layoutButtonNo(_("Next"), NULL);
  layoutButtonYes(_("Confirm"), &bmp_btn_confirm);
  oledRefresh();
}

void layoutSignIdentity(const IdentityType *identity, const char *challenge) {
  char row_proto[8 + 11 + 1] = {0};
  char row_hostport[64 + 6 + 1] = {0};
  char row_user[64 + 8 + 1] = {0};

  bool is_gpg = (strcmp(identity->proto, "gpg") == 0);

  if (identity->has_proto && identity->proto[0]) {
    if (strcmp(identity->proto, "https") == 0) {
      strlcpy(row_proto, _("Web sign in to:"), sizeof(row_proto));
    } else if (is_gpg) {
      strlcpy(row_proto, _("GPG sign for:"), sizeof(row_proto));
    } else {
      strlcpy(row_proto, identity->proto, sizeof(row_proto));
      char *p = row_proto;
      while (*p) {
        *p = toupper((int)*p);
        p++;
      }
      strlcat(row_proto, _(" login to:"), sizeof(row_proto));
    }
  } else {
    strlcpy(row_proto, _("Login to:"), sizeof(row_proto));
  }

  if (identity->has_host && identity->host[0]) {
    strlcpy(row_hostport, identity->host, sizeof(row_hostport));
    if (identity->has_port && identity->port[0]) {
      strlcat(row_hostport, ":", sizeof(row_hostport));
      strlcat(row_hostport, identity->port, sizeof(row_hostport));
    }
  } else {
    row_hostport[0] = 0;
  }

  if (identity->has_user && identity->user[0]) {
    strlcpy(row_user, _("user: "), sizeof(row_user));
    strlcat(row_user, identity->user, sizeof(row_user));
  } else {
    row_user[0] = 0;
  }

  if (is_gpg) {
    // Split "First Last <first@last.com>" into 2 lines:
    // "First Last"
    // "first@last.com"
    char *email_start = strchr(row_hostport, '<');
    if (email_start) {
      strlcpy(row_user, email_start + 1, sizeof(row_user));
      *email_start = 0;
      char *email_end = strchr(row_user, '>');
      if (email_end) {
        *email_end = 0;
      }
    }
  }

  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"),
                    _("Do you want to sign in?"),
                    row_proto[0] ? row_proto : NULL,
                    row_hostport[0] ? row_hostport : NULL,
                    row_user[0] ? row_user : NULL, challenge, NULL, NULL);
}

void layoutDecryptIdentity(const IdentityType *identity) {
  char row_proto[8 + 11 + 1] = {0};
  char row_hostport[64 + 6 + 1] = {0};
  char row_user[64 + 8 + 1] = {0};

  if (identity->has_proto && identity->proto[0]) {
    strlcpy(row_proto, identity->proto, sizeof(row_proto));
    char *p = row_proto;
    while (*p) {
      *p = toupper((int)*p);
      p++;
    }
    strlcat(row_proto, _(" decrypt for:"), sizeof(row_proto));
  } else {
    strlcpy(row_proto, _("Decrypt for:"), sizeof(row_proto));
  }

  if (identity->has_host && identity->host[0]) {
    strlcpy(row_hostport, identity->host, sizeof(row_hostport));
    if (identity->has_port && identity->port[0]) {
      strlcat(row_hostport, ":", sizeof(row_hostport));
      strlcat(row_hostport, identity->port, sizeof(row_hostport));
    }
  } else {
    row_hostport[0] = 0;
  }

  if (identity->has_user && identity->user[0]) {
    strlcpy(row_user, _("user: "), sizeof(row_user));
    strlcat(row_user, identity->user, sizeof(row_user));
  } else {
    row_user[0] = 0;
  }

  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"),
                    _("Do you want to decrypt?"),
                    row_proto[0] ? row_proto : NULL,
                    row_hostport[0] ? row_hostport : NULL,
                    row_user[0] ? row_user : NULL, NULL, NULL, NULL);
}

#if U2F_ENABLED

void layoutU2FDialog(const char *verb, const char *appname) {
  layoutDialog(&bmp_webauthn, NULL, verb, NULL, verb, _("U2F security key?"),
               NULL, appname, NULL, NULL);
}

#endif

void layoutShowPassphrase(const char *passphrase) {
  if (layoutLast != layoutShowPassphrase) {
    layoutSwipe();
  } else {
    oledClear();
  }
  const char **str =
      split_message((const uint8_t *)passphrase, strlen(passphrase), 21);
  for (int i = 0; i < 3; i++) {
    oledDrawString(0, i * 9 + 4, str[i], FONT_FIXED);
  }
  oledDrawStringCenter(OLED_WIDTH / 2, OLED_HEIGHT - 2 * 9 - 1,
                       _("Use this passphrase?"), FONT_STANDARD);
  oledHLine(OLED_HEIGHT - 21);
  layoutButtonNo(_("Cancel"), &bmp_btn_cancel);
  layoutButtonYes(_("Confirm"), &bmp_btn_confirm);
  oledRefresh();
}

#if !BITCOIN_ONLY

void layoutNEMDialog(const BITMAP *icon, const char *btnNo, const char *btnYes,
                     const char *desc, const char *line1, const char *address) {
  static char first_third[NEM_ADDRESS_SIZE / 3 + 1];
  strlcpy(first_third, address, sizeof(first_third));

  static char second_third[NEM_ADDRESS_SIZE / 3 + 1];
  strlcpy(second_third, &address[NEM_ADDRESS_SIZE / 3], sizeof(second_third));

  const char *third_third = &address[NEM_ADDRESS_SIZE * 2 / 3];

  layoutDialogSwipe(icon, btnNo, btnYes, desc, line1, first_third, second_third,
                    third_third, NULL, NULL);
}

void layoutNEMTransferXEM(const char *desc, uint64_t quantity,
                          const bignum256 *multiplier, uint64_t fee) {
  char str_out[32] = {0}, str_fee[32] = {0};

  nem_mosaicFormatAmount(NEM_MOSAIC_DEFINITION_XEM, quantity, multiplier,
                         str_out, sizeof(str_out));
  nem_mosaicFormatAmount(NEM_MOSAIC_DEFINITION_XEM, fee, NULL, str_fee,
                         sizeof(str_fee));

  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Next"), desc,
                    _("Confirm transfer of"), str_out, _("and network fee of"),
                    str_fee, NULL, NULL);
}

void layoutNEMNetworkFee(const char *desc, bool confirm, const char *fee1_desc,
                         uint64_t fee1, const char *fee2_desc, uint64_t fee2) {
  char str_fee1[32] = {0}, str_fee2[32] = {0};

  nem_mosaicFormatAmount(NEM_MOSAIC_DEFINITION_XEM, fee1, NULL, str_fee1,
                         sizeof(str_fee1));

  if (fee2_desc) {
    nem_mosaicFormatAmount(NEM_MOSAIC_DEFINITION_XEM, fee2, NULL, str_fee2,
                           sizeof(str_fee2));
  }

  layoutDialogSwipe(
      &bmp_icon_question, _("Cancel"), confirm ? _("Confirm") : _("Next"), desc,
      fee1_desc, str_fee1, fee2_desc, fee2_desc ? str_fee2 : NULL, NULL, NULL);
}

void layoutNEMTransferMosaic(const NEMMosaicDefinition *definition,
                             uint64_t quantity, const bignum256 *multiplier,
                             uint8_t network) {
  char str_out[32] = {0}, str_levy[32] = {0};

  nem_mosaicFormatAmount(definition, quantity, multiplier, str_out,
                         sizeof(str_out));

  if (definition->has_levy) {
    nem_mosaicFormatLevy(definition, quantity, multiplier, network, str_levy,
                         sizeof(str_levy));
  }

  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Next"),
                    definition->has_name ? definition->name : _("Mosaic"),
                    _("Confirm transfer of"), str_out,
                    definition->has_levy ? _("and levy of") : NULL,
                    definition->has_levy ? str_levy : NULL, NULL, NULL);
}

void layoutNEMTransferUnknownMosaic(const char *namespace, const char *mosaic,
                                    uint64_t quantity,
                                    const bignum256 *multiplier) {
  char mosaic_name[32] = {0};
  nem_mosaicFormatName(namespace, mosaic, mosaic_name, sizeof(mosaic_name));

  char str_out[32] = {0};
  nem_mosaicFormatAmount(NULL, quantity, multiplier, str_out, sizeof(str_out));

  char *decimal = strchr(str_out, '.');
  if (decimal != NULL) {
    *decimal = '\0';
  }

  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("I take the risk"),
                    _("Unknown Mosaic"), _("Confirm transfer of"), str_out,
                    _("raw units of"), mosaic_name, NULL, NULL);
}

void layoutNEMTransferPayload(const uint8_t *payload, size_t length,
                              bool encrypted) {
  if (length >= 1 && payload[0] == 0xFE) {
    char encoded[(length - 1) * 2 + 1];
    memset(encoded, 0, sizeof(encoded));

    data2hex(&payload[1], length - 1, encoded);

    const char **str =
        split_message((uint8_t *)encoded, sizeof(encoded) - 1, 16);
    layoutDialogSwipe(
        &bmp_icon_question, _("Cancel"), _("Next"),
        encrypted ? _("Encrypted hex data") : _("Unencrypted hex data"), str[0],
        str[1], str[2], str[3], NULL, NULL);
  } else {
    const char **str = split_message(payload, length, 16);
    layoutDialogSwipe(
        &bmp_icon_question, _("Cancel"), _("Next"),
        encrypted ? _("Encrypted message") : _("Unencrypted message"), str[0],
        str[1], str[2], str[3], NULL, NULL);
  }
}

void layoutNEMMosaicDescription(const char *description) {
  const char **str =
      split_message((uint8_t *)description, strlen(description), 16);
  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Next"),
                    _("Mosaic Description"), str[0], str[1], str[2], str[3],
                    NULL, NULL);
}

void layoutNEMLevy(const NEMMosaicDefinition *definition, uint8_t network) {
  const NEMMosaicDefinition *mosaic = NULL;
  if (nem_mosaicMatches(definition, definition->levy_namespace,
                        definition->levy_mosaic, network)) {
    mosaic = definition;
  } else {
    mosaic = nem_mosaicByName(definition->levy_namespace,
                              definition->levy_mosaic, network);
  }

  char mosaic_name[32] = {0};
  if (mosaic == NULL) {
    nem_mosaicFormatName(definition->levy_namespace, definition->levy_mosaic,
                         mosaic_name, sizeof(mosaic_name));
  }

  char str_out[32] = {0};

  switch (definition->levy) {
    case NEMMosaicLevy_MosaicLevy_Percentile:
      bn_format_amount(definition->fee, NULL, NULL, 0, str_out,
                       sizeof(str_out));

      layoutDialogSwipe(
          &bmp_icon_question, _("Cancel"), _("Next"), _("Percentile Levy"),
          _("Raw levy value is"), str_out, _("in"),
          mosaic ? (mosaic == definition ? _("the same mosaic") : mosaic->name)
                 : mosaic_name,
          NULL, NULL);
      break;

    case NEMMosaicLevy_MosaicLevy_Absolute:
    default:
      nem_mosaicFormatAmount(mosaic, definition->fee, NULL, str_out,
                             sizeof(str_out));
      layoutDialogSwipe(
          &bmp_icon_question, _("Cancel"), _("Next"), _("Absolute Levy"),
          _("Levy is"), str_out,
          mosaic ? (mosaic == definition ? _("in the same mosaic") : NULL)
                 : _("in raw units of"),
          mosaic ? NULL : mosaic_name, NULL, NULL);
      break;
  }
}

#endif

void layoutConfirmAutoLockDelay(uint32_t delay_ms) {
  char line[sizeof("after 4294967296 minutes?")] = {0};

  const char *unit = _("second");
  uint32_t num = delay_ms / 1000U;

  if (delay_ms >= 60 * 60 * 1000) {
    unit = _("hour");
    num /= 60 * 60U;
  } else if (delay_ms >= 60 * 1000) {
    unit = _("minute");
    num /= 60U;
  }

  strlcpy(line, _("after "), sizeof(line));
  size_t off = strlen(line);
  bn_format_amount(num, NULL, NULL, 0, &line[off], sizeof(line) - off);
  strlcat(line, " ", sizeof(line));
  strlcat(line, unit, sizeof(line));
  if (num > 1) {
    strlcat(line, "s", sizeof(line));
  }
  strlcat(line, "?", sizeof(line));
  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                    _("Do you really want to"), _("auto-lock your device"),
                    line, NULL, NULL, NULL);
}

void layoutConfirmSafetyChecks(SafetyCheckLevel safety_ckeck_level) {
  if (safety_ckeck_level == SafetyCheckLevel_Strict) {
    // Disallow unsafe actions. This is the default.
    layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                      _("Do you really want to"), _("enforce strict safety"),
                      _("checks?"), _("(Recommended.)"), NULL, NULL);
  } else if (safety_ckeck_level == SafetyCheckLevel_PromptTemporarily) {
    // Ask user before unsafe action. Reverts to Strict after reboot.
    layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                      _("Do you really want to"), _("be temporarily able"),
                      _("to approve some"), _("actions which might"),
                      _("be unsafe?"), NULL);
  }
}

void layoutConfirmHash(const BITMAP *icon, const char *description,
                       const uint8_t *hash, uint32_t len) {
  const char **str = split_message_hex(hash, len);

  layoutSwipe();
  oledClear();
  oledDrawBitmap(0, 0, icon);
  oledDrawString(20, 0 * 9, description, FONT_STANDARD);
  oledDrawString(20, 1 * 9, str[0], FONT_FIXED);
  oledDrawString(20, 2 * 9, str[1], FONT_FIXED);
  oledDrawString(20, 3 * 9, str[2], FONT_FIXED);
  oledDrawString(20, 4 * 9, str[3], FONT_FIXED);
  oledHLine(OLED_HEIGHT - 13);

  layoutButtonNo(_("Cancel"), &bmp_btn_cancel);
  layoutButtonYes(_("Confirm"), &bmp_btn_confirm);
  oledRefresh();
}

void layoutConfirmOwnershipProof(void) {
  layoutDialogSwipe(&bmp_icon_question, _("Cancel"), _("Confirm"), NULL,
                    _("Do you want to"), _("create a proof of"),
                    _("ownership?"), NULL, NULL, NULL);
}
