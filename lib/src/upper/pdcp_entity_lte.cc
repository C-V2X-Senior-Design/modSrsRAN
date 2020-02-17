/*
 * Copyright 2013-2019 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srslte/upper/pdcp_entity_lte.h"
#include "srslte/common/security.h"

namespace srslte {

pdcp_entity_lte::pdcp_entity_lte(srsue::rlc_interface_pdcp* rlc_,
                                 srsue::rrc_interface_pdcp* rrc_,
                                 srsue::gw_interface_pdcp*  gw_,
                                 srslte::timer_handler*     timers_,
                                 srslte::log*               log_) :
  pdcp_entity_base(timers_, log_),
  rlc(rlc_),
  rrc(rrc_),
  gw(gw_)
{
}

pdcp_entity_lte::~pdcp_entity_lte() {}

void pdcp_entity_lte::init(uint32_t lcid_, pdcp_config_t cfg_)
{
  lcid          = lcid_;
  cfg           = cfg_;
  active        = true;
  tx_count      = 0;
  rx_count      = 0;
  do_integrity  = false;
  do_encryption = false;

  if (is_srb()) {
    reordering_window = 0;
  } else if (is_drb()) {
    reordering_window = 2048;
  }

  rx_hfn                    = 0;
  next_pdcp_rx_sn           = 0;
  maximum_pdcp_sn           = (1 << cfg.sn_len) - 1;
  last_submitted_pdcp_rx_sn = maximum_pdcp_sn;
  log->info("Init %s with bearer ID: %d\n", rrc->get_rb_name(lcid).c_str(), cfg.bearer_id);
  log->info("SN len bits: %d, SN len bytes: %d, reordering window: %d, Maximum SN %d\n",
            cfg.sn_len,
            cfg.hdr_len_bytes,
            reordering_window,
            maximum_pdcp_sn);
}

// Reestablishment procedure: 36.323 5.2
void pdcp_entity_lte::reestablish()
{
  log->info("Re-establish %s with bearer ID: %d\n", rrc->get_rb_name(lcid).c_str(), cfg.bearer_id);
  // For SRBs
  if (is_srb()) {
    tx_count        = 0;
    rx_count        = 0;
    rx_hfn          = 0;
    next_pdcp_rx_sn = 0;
  } else {
    // Only reset counter in RLC-UM
    if (rlc->rb_is_um(lcid)) {
      tx_count        = 0;
      rx_count        = 0;
      rx_hfn          = 0;
      next_pdcp_rx_sn = 0;
    }
  }
}

// Used to stop/pause the entity (called on RRC conn release)
void pdcp_entity_lte::reset()
{
  active = false;
  if (log) {
    log->debug("Reset %s\n", rrc->get_rb_name(lcid).c_str());
  }
}

// GW/RRC interface
void pdcp_entity_lte::write_sdu(unique_byte_buffer_t sdu, bool blocking)
{
  log->info_hex(sdu->msg,
                sdu->N_bytes,
                "TX %s SDU, SN: %d, do_integrity = %s, do_encryption = %s",
                rrc->get_rb_name(lcid).c_str(),
                tx_count,
                (do_integrity) ? "true" : "false",
                (do_encryption) ? "true" : "false");

  {
    std::unique_lock<std::mutex> lock(mutex);

    if (is_srb()) {
      pdcp_pack_control_pdu(tx_count, sdu.get());
      if (do_integrity) {
        integrity_generate(sdu->msg, sdu->N_bytes - 4, tx_count, &sdu->msg[sdu->N_bytes - 4]);
      }
    }

    if (is_drb()) {
      if (12 == cfg.sn_len) {
        pdcp_pack_data_pdu_long_sn(tx_count, sdu.get());
      } else {
        pdcp_pack_data_pdu_short_sn(tx_count, sdu.get());
      }
    }

    if (do_encryption) {
      cipher_encrypt(
          &sdu->msg[cfg.hdr_len_bytes], sdu->N_bytes - cfg.hdr_len_bytes, tx_count, &sdu->msg[cfg.hdr_len_bytes]);
      log->info_hex(sdu->msg, sdu->N_bytes, "TX %s SDU (encrypted)", rrc->get_rb_name(lcid).c_str());
    }
    tx_count++;
  }

  rlc->write_sdu(lcid, std::move(sdu), blocking);
}

// RLC interface
void pdcp_entity_lte::write_pdu(unique_byte_buffer_t pdu)
{
  log->info_hex(pdu->msg,
                pdu->N_bytes,
                "RX %s PDU (%d B), do_integrity = %s, do_encryption = %s",
                rrc->get_rb_name(lcid).c_str(),
                pdu->N_bytes,
                (do_integrity) ? "true" : "false",
                (do_encryption) ? "true" : "false");

  // Sanity check
  if (pdu->N_bytes <= cfg.hdr_len_bytes) {
    return;
  }

  std::unique_lock<std::mutex> lock(mutex);
  if (is_srb()) {
    handle_srb_pdu(std::move(pdu));
  } else if (is_drb() && rlc->rb_is_um(lcid)) {
    handle_um_drb_pdu(std::move(pdu));
  } else if (is_drb() && !rlc->rb_is_um(lcid)) {
    handle_am_drb_pdu(std::move(pdu));
  } else {
    log->error("Invalid PDCP/RLC configuration");
  }
}

/****************************************************************************
 * Rx data/control handler functions
 * Ref: 3GPP TS 36.323 v10.1.0 Section 5.1.2
 ***************************************************************************/
// SRBs (5.1.2.2)
void pdcp_entity_lte::handle_srb_pdu(srslte::unique_byte_buffer_t pdu)
{
  log->info_hex(pdu->msg, pdu->N_bytes, "RX %s PDU", rrc->get_rb_name(lcid).c_str());

  // Read recvd SN from header
  uint32_t sn = SN(pdu->msg[0]); 

  log->debug("RX SRB PDU. Next_PDCP_RX_SN %d, SN %d", next_pdcp_rx_sn, sn);

  // Estimate COUNT for integrity check and decryption
  uint32_t count;
  if (sn < next_pdcp_rx_sn){
    count = COUNT(rx_hfn + 1, sn);
  } else {
    count = COUNT(rx_hfn, sn);
  }
 
  // Perform decription  
  if (do_encryption) {
    cipher_decrypt(&pdu->msg[1], pdu->N_bytes - 1, count, pdu->msg);
    log->info_hex(pdu->msg, pdu->N_bytes, "RX %s PDU (decrypted)", rrc->get_rb_name(lcid).c_str());
  }

  // Extract MAC
  uint8_t mac[4];
  extract_mac(pdu, mac);

  // Perfrom integrity checks
  if (do_integrity) {
    if (not integrity_verify(pdu->msg, pdu->N_bytes, count, mac)) {
      log->error_hex(pdu->msg, pdu->N_bytes, "%s Dropping PDU", rrc->get_rb_name(lcid).c_str());
      return; // Discard
    }
  }

  // Discard header
  pdu->msg++;

  // Update state variables
  if (sn < next_pdcp_rx_sn) {
    rx_hfn++;
  }
  next_pdcp_rx_sn = sn + 1;

  if (next_pdcp_rx_sn > maximum_pdcp_sn) {
    next_pdcp_rx_sn = 0;
    rx_hfn++;
  }

  // Pass to upper layers
  log->info_hex(pdu->msg, pdu->N_bytes, "Passing SDU to upper layers");
  rrc->write_pdu(lcid, std::move(pdu));
  return;
}

// DRBs mapped on RLC UM (5.1.2.1.3)
void pdcp_entity_lte::handle_um_drb_pdu(srslte::unique_byte_buffer_t pdu)
{
  uint32_t sn;
  if (12 == cfg.sn_len) {
    pdcp_unpack_data_pdu_long_sn(pdu.get(), &sn);
  } else {
    pdcp_unpack_data_pdu_short_sn(pdu.get(), &sn);
  }

  if (sn < next_pdcp_rx_sn) {
    rx_hfn++;
  }

  uint32_t count = (rx_hfn << cfg.sn_len) | sn;
  if (do_encryption) {
    cipher_decrypt(pdu->msg, pdu->N_bytes, count, pdu->msg);
    log->debug_hex(pdu->msg, pdu->N_bytes, "RX %s PDU (decrypted)", rrc->get_rb_name(lcid).c_str());
  }

  next_pdcp_rx_sn = sn + 1;
  if (next_pdcp_rx_sn > maximum_pdcp_sn) {
    next_pdcp_rx_sn = 0;
    rx_hfn++;
  }

  // Pass to upper layers
  log->info_hex(pdu->msg, pdu->N_bytes, "RX %s PDU SN: %d", rrc->get_rb_name(lcid).c_str(), sn);
  gw->write_pdu(lcid, std::move(pdu));
  return;
}

// DRBs mapped on RLC AM, without re-ordering (5.1.2.1.2)
void pdcp_entity_lte::handle_am_drb_pdu(srslte::unique_byte_buffer_t pdu)
{
  uint32_t sn, count;
  pdcp_unpack_data_pdu_long_sn(pdu.get(), &sn);

  int32_t last_submit_diff_sn     = last_submitted_pdcp_rx_sn - sn;
  int32_t sn_diff_last_submit     = sn - last_submitted_pdcp_rx_sn;
  int32_t sn_diff_next_pdcp_rx_sn = sn - next_pdcp_rx_sn;

  log->debug("RX HFN: %d, SN: %d, Last_Submitted_PDCP_RX_SN: %d, Next_PDCP_RX_SN %d\n",
             rx_hfn,
             sn,
             last_submitted_pdcp_rx_sn,
             next_pdcp_rx_sn);

  // Handle PDU
  if ((0 <= sn_diff_last_submit && sn_diff_last_submit > (int32_t)reordering_window) ||
      (0 <= last_submit_diff_sn && last_submit_diff_sn < (int32_t)reordering_window)) {
    log->warning("|SN - last_submitted_sn| is larger than re-ordering window.\n");
    if (sn > next_pdcp_rx_sn) {
      count = (rx_hfn - 1) << cfg.sn_len | sn;
    } else {
      count = rx_hfn << cfg.sn_len | sn;
    }
    return; // Discard
  } else if ((int32_t)(next_pdcp_rx_sn - sn) > (int32_t)reordering_window) {
    log->debug("(Next_PDCP_RX_SN - SN) is larger than re-ordering window.\n");
    rx_hfn++;
    count           = (rx_hfn << cfg.sn_len) | sn;
    next_pdcp_rx_sn = sn + 1;
  } else if (sn_diff_next_pdcp_rx_sn >= (int32_t)reordering_window) {
    log->debug("(SN - Next_PDCP_RX_SN) is larger or equal than re-ordering window.\n");
    count = ((rx_hfn - 1) << cfg.sn_len) | sn;
  } else if (sn >= next_pdcp_rx_sn) {
    log->debug("SN is larger or equal than Next_PDCP_RX_SN.\n");
    count           = (rx_hfn << cfg.sn_len) | sn;
    next_pdcp_rx_sn = sn + 1;
    if (next_pdcp_rx_sn > maximum_pdcp_sn) {
      next_pdcp_rx_sn = 0;
      rx_hfn++;
    }
  } else if (sn < next_pdcp_rx_sn) {
    log->debug("SN is smaller than Next_PDCP_RX_SN.\n");
    count = (rx_hfn << cfg.sn_len) | sn;
  }

  // Decrypt
  cipher_decrypt(pdu->msg, pdu->N_bytes, count, pdu->msg);
  log->debug_hex(pdu->msg, pdu->N_bytes, "RX %s PDU (decrypted)", rrc->get_rb_name(lcid).c_str());

  // Update info on last PDU submitted to upper layers
  last_submitted_pdcp_rx_sn = sn;

  // Pass to upper layers
  log->info_hex(pdu->msg, pdu->N_bytes, "RX %s PDU SN: %d", rrc->get_rb_name(lcid).c_str(), sn);
  gw->write_pdu(lcid, std::move(pdu));
  return;
}

/****************************************************************************
 * Security functions
 ***************************************************************************/
uint32_t pdcp_entity_lte::get_dl_count()
{
  return rx_count;
}

uint32_t pdcp_entity_lte::get_ul_count()
{
  return tx_count;
}

void pdcp_entity_lte::get_bearer_status(uint16_t* dlsn, uint16_t* dlhfn, uint16_t* ulsn, uint16_t* ulhfn)
{
  if (cfg.rb_type == PDCP_RB_IS_DRB) {
    if (12 == cfg.sn_len) {
      *dlsn  = (uint16_t)(tx_count & 0xFFFu);
      *dlhfn = (uint16_t)((tx_count - *dlsn) >> 12u);
      *ulsn  = (uint16_t)(rx_count & 0xFFFu);
      *ulhfn = (uint16_t)((rx_count - *ulsn) >> 12u);
    } else {
      *dlsn  = (uint16_t)(tx_count & 0x7Fu);
      *dlhfn = (uint16_t)((tx_count - *dlsn) >> 7u);
      *ulsn  = (uint16_t)(rx_count & 0x7Fu);
      *ulhfn = (uint16_t)((rx_count - *ulsn) >> 7u);
    }
  } else { // is control
    *dlsn  = (uint16_t)(tx_count & 0x1Fu);
    *dlhfn = (uint16_t)((tx_count - *dlsn) >> 5u);
    *ulsn  = (uint16_t)(rx_count & 0x1Fu);
    *ulhfn = (uint16_t)((rx_count - *ulsn) >> 5u);
  }
}

/****************************************************************************
 * Pack/Unpack helper functions
 * Ref: 3GPP TS 36.323 v10.1.0
 ***************************************************************************/
void pdcp_pack_control_pdu(uint32_t sn, byte_buffer_t* sdu)
{
  // Make room and add header
  sdu->msg--;
  sdu->N_bytes++;
  *sdu->msg = sn & 0x1F;

  // Add MAC
  sdu->msg[sdu->N_bytes++] = (PDCP_CONTROL_MAC_I >> 24) & 0xFF;
  sdu->msg[sdu->N_bytes++] = (PDCP_CONTROL_MAC_I >> 16) & 0xFF;
  sdu->msg[sdu->N_bytes++] = (PDCP_CONTROL_MAC_I >> 8) & 0xFF;
  sdu->msg[sdu->N_bytes++] = PDCP_CONTROL_MAC_I & 0xFF;
}

void pdcp_unpack_control_pdu_sn(byte_buffer_t* sdu, uint32_t* sn)
{
  // Strip header
  *sn = sdu->msg[0] & 0x1Fu;
  sdu->msg++;
  sdu->N_bytes--;
}

void pdcp_pack_data_pdu_short_sn(uint32_t sn, byte_buffer_t* sdu)
{
  // Make room and add header
  sdu->msg--;
  sdu->N_bytes++;
  sdu->msg[0] = (PDCP_D_C_DATA_PDU << 7) | (sn & 0x7Fu);
}


void pdcp_unpack_data_pdu_short_sn(byte_buffer_t* sdu, uint32_t* sn)
{
  // Strip header
  *sn = sdu->msg[0] & 0x7Fu;
  sdu->msg++;
  sdu->N_bytes--;
}

void pdcp_pack_data_pdu_long_sn(uint32_t sn, byte_buffer_t* sdu)
{
  // Make room and add header
  sdu->msg -= 2;
  sdu->N_bytes += 2;
  sdu->msg[0] = (PDCP_D_C_DATA_PDU << 7) | ((sn >> 8) & 0x0Fu);
  sdu->msg[1] = sn & 0xFFu;
}

void pdcp_unpack_data_pdu_long_sn(byte_buffer_t* sdu, uint32_t* sn)
{
  // Strip header
  *sn = (sdu->msg[0] & 0x0Fu) << 8;
  *sn |= sdu->msg[1];
  sdu->msg += 2;
  sdu->N_bytes -= 2;
}
} // namespace srslte
