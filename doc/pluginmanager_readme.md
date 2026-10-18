# TUS Plugin Manager

Installs and updates The Usual Suspects plugins, so that keeping them current does not
mean downloading archives and unpacking them into the right folders by hand.

The window lists every product we publish. Each row has a column per plugin format
showing the version that is installed, a dropdown of every version you could have, and a
button that does whatever that column needs: install it, upgrade it, put an older one
back, or remove it. The **All** column does the same for a whole row, and the buttons
above the list do it for every row at once.

`tuspm` beside it is the same program without a window, for a script or a build machine.

## Where it keeps things

The same place the plugins keep theirs, one folder along:

| | |
|---|---|
| Windows | `Documents\The Usual Suspects\PluginManager\config` |
| macOS | `~/Documents/The Usual Suspects/PluginManager/config` |
| Linux | `$XDG_DATA_HOME/The Usual Suspects/PluginManager/config`, or `~/.local/share/…` |

In that folder:

- `settings.xml` — where each format is installed, and any per-product exceptions.
- `installed.json` — what this program installed, which version, and which files the
  archive put down. Removing a plugin uses that list, so an install this program did not
  do leaves whatever came with it behind.
- `selection.xml` — which formats you ticked, so that somebody who installs one format only
  does not have to say so again on every start. Nothing is ticked on a first start, and a
  product or format that appears later starts unticked too.
- `credentials.xml` — logins, **in plain text**. Delete the file to forget them.
- `log.txt` — see below.

`--settings <folder>` puts all of it somewhere else, for trying something without
standing on the copy you actually use. Works on both programs.

## Where the plugins go

Preferences holds one location per format, and says whether it is the default for your
system or something you chose. A location that does not exist yet is created by the first
install.

On Windows the plugin folders belong to the system, so installing into them asks for
administrator rights. The prompt comes from this program starting a second copy of itself
with those rights — only to copy files that are already on your disk. Nothing is
downloaded with them, and declining the prompt leaves everything as it was.

If a plugin cannot be replaced because a host has it open, the install stops before it
has changed anything and names the file. Close the host and try again.

## Builds you can see

Public releases need no login.

A **donator login** adds the builds for a product you support.

Logins are checked when you enter them and only stored once the server has accepted them.

A login can also be **handed to you as a file or a line of text**, instead of a folder name,
a user and a password to type. Drop the file on the window, or copy the line and press
Ctrl+V. One can carry several. Either way it says what it is about to add and asks first,
and each login is still checked against the server before it is kept.

The password in such a file is scrambled so that it cannot be read at a glance in a chat or
a screenshot. That is all it is: this program has to read it, so anybody with this program
can read it too. Treat the file as the password it carries, and change the password on the
server if one gets out.

## When something goes wrong

`log.txt` in the folder above records which server answered, what was downloaded, where
it went, and what the matter was when an install or a removal did not work. It keeps the
last few hundred kilobytes.

**Send `log.txt` and `installed.json`** with a report. Between them they say what this
program believed and what it actually did, which a description of the symptom cannot.

## tuspm

```
tuspm list [<product>]
tuspm install <product> [--format <format>] [--version <version>]
tuspm uninstall <product> [--format <format>]
tuspm login <internal|donators> [<product>] --user <user> --password <password>
tuspm logout <internal|donators> [<product>]
tuspm share <product> [<product>...] [--out <file>]
tuspm paths [<format> <location>|<format> default]
```

`share` turns logins this machine already has into the one line described above, to send or
to paste. Only the folders you name, never all of them.

`list` prints one build per line, tab separated: product, format, version, channel, and
the version of that format that is installed. A script can cut that up.

It lists the effect builds too, under their own names - `OsirusFX` and the like - because
that is the name `install` takes for them. The window folds them into the plugin they
belong to; here they are products like any other.

`--user` and `--password` on `list` and `install` are for that run only and are not
stored, so a build machine does not leave a password behind.

`--settings <folder>` works with any of them.
