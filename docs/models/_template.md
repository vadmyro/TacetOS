<!-- Template for one model page. Copy to docs/models/<id>.md, replace every <...>, delete these comments. The values come from models/models.toml, which is the source of truth: if the two disagree, fix the page. -->

# <name>

<One sentence: what this iPod is, which years it was sold, which family it belongs to.>

## At a glance

| | |
|---|---|
| **Id** | `<id>` |
| **Family** | <family> |
| **Screen** | <width> x <height>, <depth_bits> bits, <colour or grayscale>, <diagonal_inches> inches |
| **Storage** | <kind>, <form_factor>, <capacities_gb> GB |
| **Install method** | <method> |
| **Dual boot required** | <yes or no> |
| **Bootloader target** | `<bootloader_target>` |
| **Support** | <tested on real hardware, experimental, or not supported> |

## How to recognise it

<Front and back details a user can check: click wheel or buttons, screen, colour, thickness, the model number printed on the back. Add a photo only if it is yours or free to use.>

## USB ids

| Mode | USB id | When you see it |
|---|---|---|
| Normal | `<vendor:product>` | <plugged in, idle or syncing> |
| Disk Mode | `<vendor:product>` | <how to enter it on this model> |
| DFU | `<vendor:product>` | <how to enter it, or "not available"> |

## Installing TacetOS

<The steps in order, for this model only. Say which mode the iPod must be in and what the app does at each step. State plainly whether the install is dual boot, and how to get back to the original firmware.>

## What is tested

<What was run on a real iPod of this model, by whom, on which day, and what is still untested. A model with no real test stays marked experimental.>

## Known issues

<Short list, or "None known".>

## Sources

<One line per source id used for this model in `models/models.toml`, with the title, the location and the day it was checked. List the facts that are still in `to_verify` and what is needed to confirm them.>
