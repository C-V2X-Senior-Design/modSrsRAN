## Changelog for debugging pssch_ue (modsrsran)
##### Author: Julia Zeng (zjulia@bu.edu)

<br> </br>

## Issue No.1	
The srsRAN release 21.10 implementation of pssch_ue.c (found in
modSrsRan/build/lib/examples) was giving a floating point exception error 
(GitHub issue in srsRAN open source found here: 
https://github.com/srsran/srsRAN/issues/838).


**Problematic filename:function:linenum?**
- /lib/src/phy/sync/sync.c:srsran_sync_set_cp:446
- ``` q->nof_symbols = q->frame_size / (q->fft_size + q->cp_len) - 1;   // ADDED COMMENT: FLOATING POINT EXCEPTION HAPPENING HERE ```

**What is the issue in that line?**
- Both q->fft_size and q->cp_len are 0 which result in a divide by 0 error. 

**How to recreate the error and debug it?**
- Revert to commit 86a094c98e77b32ed0a1edcb053dabbed4b3c48d
- Add -g flag to cmake line in maker.sh to enable debugging
- Run: ./maker.sh
- Navigate to pssch_ue executable: cd /build/lib/examples
- Run gdb: gdb ./pssch_ue
- Add breakpoints: 
	- (1) b main
	- (2) b ue_sync.c:srsran_ue_sync_set_cell
	- (3) b sync.c:srsran_sync_set_cp
- Step with 'continue' (c) until you hit error and you should see that you hit the error in sync.c:srsran_sync_set_cp:446
- Revert back to this commit (which includes debugging print statements) and run the previous steps if you'd like

**How I "solved" this issue?**
- srsran_sync_set_cp is called twice in ue_sync.c:srsran_ue_sync_set_cell:347 and ue_sync.c:srsran_ue_sync_set_cell:350, so I commented them out, LOL. Run now, floating point exception should disappear. 
- Another "solution" is to set the FFT and CP_LEN values to something non-zero, to do this uncomment lines ue_sync.c:srsran_ue_sync_set_cell:346 and ue_sync.c:srsran_ue_sync_set_cell:349. I have no clue what this does... but it runs without breaking .... :DD


<br> </br>


## Issue No.2
No packets were passing the srsran_pssch_decode in pssch_ue.c, i.e.
num_decoded_tb always equaled 0, and therefore no PCAPs were generated.


**Problemtatic filename:function:linenum?**
- /lib/src/phy/phch/pssch.c:srsran_pssch_decode:465
- /lib/src/phy/phch/pssch.c:srsran_pssch_decode:487

**What is the issue in that line?**
- It is not passing the checksum, i.e. srsran_bit_diff(), which essentially checks that every bit is the same and as a result an error is returned to pssch_ue indicating the packet cannot be decoded. 

**How to recreate the error?**
- Add ERROR("Checksum error") error printing statements to those two places
- Run pssch_ue.c. You should now see those "Checksum error" error messages printed to the console

**How I "solved" this issue?**
- Commented out CRC in /lib/src/phy/phch/pssch.c:srsran_pssch_decode:465 and /lib/src/phy/phch/pssch.c:srsran_pssch_decode:487
- Now, pssch_ue will generate PCAPs in tmp/pssch.pcap, but this is garbage


<br> </br>


## Issue No.3
(Cross-documented in C-V2X traffic generator fork)<br> </br>
This is not an issue, but rather an explanation for why we do not need to
account for the non-adjacent C-V2X subchannelization scheme, just the adjacent scheme.
Refer to Fabian Eckermann's paper *SDR-based Open-Source C-V2X Traffic Generator for Stress 
Testing Vehicular Communication* for explanation of C-V2X subchannelization schemes. 

- cv2x-traffic-generator-fork/lib/src/phy/common/phy_common_sl.c:407 defines ```* @param adjacency_pscch_pssch subchannelization adjacency ```
- cv2x_traffic_generator.c:372 calls srslte_sl_comm_resource_pool_set_cfg() which is hardcoded to set adjacency_pscch_pssch=true.
- Why I think true=adjacent and false=non-adjacent: cv2x-traffic-generator-fork/lib/src/phy/ue/ue_sl.c:658
  ``` 
  if (q->sl_comm_resource_pool.adjacency_pscch_pssch) {
	pscch_prb_start_idx = sub_channel_idx * q->sl_comm_resource_pool.size_sub_channel; 
  } else {
	pscch_prb_start_idx = sub_channel_idx * 2;
  }
  ```
  If adjacency_pscch_pssch=true, then pssch starting block will be a multiple of the subchannel size, 
  else it is a multiple of 2. 

- modSrsRAN/lib/src/phy/common/phy_common_sl.c:srsran_sl_comm_resource_pool_get_default_config:346, which is called in pssch_ue, hardcodes adjacency_pscch_pssch=true.
