# Changelog

## [2022.2.21] add_probes

### Added

- added `iq_probe` to `cc_worker.cc`

### Changed

- chagned `main_hw_loop.sh` to accept input time lengths

## [2022.2.21] add_probes

### Added

- added function `output_probe` to `cc_worker.cc` and `cc_worker.h`

- added script `run_probe.sh` which refreshes the `probes` folder and runs `srsenb`

- added probes to:
	- `sched_grid.cc`
	- `sched_base.cc`
	- `sched_time_pf.cc`
	- `sched_harq.cc`
	- `sched_dl_cqi.cc`
	- `sched_phy_resource.cc`
	- `sched_ue.cc`

### Changed

- commented out old iq data output for now

## [2022.2.4] port_local_changes

### Added

- added function `ieee_float_to_hex` to `cc_worker.cc` and `cc_worker.h` (source: http://www.cplusplus.com/forum/general/63755/)

- added `maker.sh` to streamline rebuilding and installation

### Changed

- modified functions `cc_worker:read_pusch_d` and `cc_worker:read_pucch_d` in `cc_worker.cc` to output hex IQ data to terminal

- modified `CMakeLists.txt` to exclusively rebuild srsenb
