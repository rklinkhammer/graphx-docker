# SDR node examples

The [simulated](simulated/README.md) and [external](external/README.md) profiles
are authored v3 graphs. Their catalogs describe raw samples, control, mutual TLS
credential references and observation. Validation and normalization are implemented;
use the common compiled runner. Physical startup stays gated; the laboratory
substitute requires explicit `compile --laboratory laboratory-radio` selection.

The simulated graph supports Linux, OrbStack and Lima target validation. The
external graph requires Linux or Lima for its logical OVS attachments. Physical
radio ownership stays external. Optional laboratory simulation is an explicit
scenario action and never starts during normalization.
