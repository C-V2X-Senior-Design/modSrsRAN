srsRAN
======

srsRAN is a 4G/5G software radio suite developed by [SRS](http://www.srs.io).

See the [srsRAN project pages](https://www.srsran.com) for information, guides and project news.

The srsRAN suite includes:
  * srsUE - a full-stack SDR 4G/5G-NSA UE application (5G-SA coming soon)
  * srsENB - a full-stack SDR 4G/5G-NSA eNodeB application (5G-SA coming soon)
  * srsEPC - a light-weight 4G core network implementation with MME, HSS and S/P-GW

For application features, build instructions and user guides see the [srsRAN documentation](https://docs.srsran.com).

For license details, see LICENSE file.

Team 13
=======

Team 13 is a group of Boston University Students modifying and using the srsRAN library to model and study C-V2X traffic.

Building & Installation
=======

This repo is configured for ease of rebuilding on machines that currently have srsRAN built and installed.

In order to build this library on a new machine, change the following lines in `CMakeLists.txt` from ""OFF" to "ON":

```
option(ENABLE_SRSUE          "Build srsUE application"                  OFF)
option(ENABLE_SRSUE          "Build srsUE application"                  OFF)
option(ENABLE_BLADERF        "Enable BladeRF"                           OFF)
option(ENABLE_SOAPYSDR       "Enable SoapySDR"                          OFF)
option(ENABLE_SKIQ           "Enable Sidekiq SDK"                       OFF)
```

Using Probes
=======

Standard print statements are disabled in certain parts of the SrsRAN codebase. The `output_probe(string text, string file_name)` function allows the user to determine whether or not a piece of code is reached during the execution of SrsRAN code by printing to files found in the `/probes` folder instead.

In order to use `output_probe`, add the following header to the file(s) of interest:
```
#include "srsenb/hdr/phy/lte/cc_worker.h"
```

Then, at specific points of interest, add the fuction call:
```
output_probe(text, file_name);
```
* `text` is a unique identifier for this location in the code; use `text = __name__` if only checking one location per file.

* `file_name` is the name of the file/file path to be created/appended to in the `/probes` directory

Lines in the output file will consist of a unix timestamp and the value of the `text` parameter. An example output can be found [here](https://github.com/C-V2X-Senior-Design/modSrsRAN/blob/add_probes/probes/rbgmask_t_probe.txt)

Once all probes are added, run the following commands to generate your probe outputs:
```
./maker.sh # rebuild with the added probes
./run_probe.sh
```
