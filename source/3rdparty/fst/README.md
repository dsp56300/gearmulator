FST - Free Studio Technology: header files for building audio plugins
=====================================================================

this is a simple header file
to allow the compilation of audio plugins and audio plugin hosts following
the industrial standard of Steinberg's VST(2).

Steinberg provides a VST2 SDK, but:
- it has a proprietary license
- is no longer available from Steinberg (as they removed the VST2-SDK to push
  VST3)

FST is the attempt to create a bona-fide reverse-engineered header file,
that is created without reference to the official VST SDK and without its
developer(s) having agreed to the VST SDK license agreement.

All reverse-engineering steps are documented in the [REVERSE_ENGINEERING](docs/REVERSE_ENGINEERING.md)
document.

## USING
You are free to use FST under our [LICENSE terms](#licensing).

However, Steinberg has trademarked the `VST` name, and explicitly forbids
anyone from using this name without agreeing to *their* LICENSE terms.

If you plan to release audio plugins based on the FST Interface, you are
not allowed to call them `VST` or even say that they are `VST Compatible`.

## BONA-FIDA REVERSE-ENGINEERING

Care is being taken to not break any license agreement or copyright terms.
We also take care not to expose or incriminate any Open Source
developer/project of unwittingly (or not) violating copyright terms or
license agreements.

Our self-imposed rules for this purpose are:
- Do not use any material from Steinberg
  - Never accept Steinberg's proprietary license agreement
  - Never obtain the VST(2)-SDK, by whatever means
- Only use VST-enabled binaries and sources (hosts and plugins) from
  commercial entities that do not require accepting any license terms.
- Do not use existing reverse-engineering attempts of the SDK

### Some reasoning
We do not consider ourselves bound by Steinberg's license if we never accept it
explicitly or implicitly (e.g. by obtaining a version of the VST-SDKs
- even if it happens to be bundled with some 3rd party project).

We only use Closed Source binaries from commercial entities under the assumption
that whowever makes their living from (also) selling plugins, will have a right
to do so (e.g. they are actually allowed to use the SDKs they are using, most
likely by coming to an agreement with the SDK vendors).
Conversely, we do not use binaries from independent Open Source developers
as it is not our job to track down the legitimacy of their use of SDKs.

We only use binaries that do not require us to actively accept license
agreements, as they might contain clauses that prevent reverse-engineering.

There are a couple of existing reverse-engineering attempts of the SDK, but we
haven't found any documentation on how they were created.
Therefore we cannot rule out that they might have been written with the use of
the original SDK (by this we do not imply that creating these attempts actually
infringed any copyright or license terms - only that we cannot be sure about it).

## VERSIONING

The versioning scheme of FST basically follows [semantic versioning](https://semver.org):

Whenever a new opcode, etc. is discovered, the minor version number is
increased. Thus the minor version number (roughly) reflects the number of
discovered symbols.

The micro version number is increased whenever a release is made without new
symbols (e.g. for a compatibility fix for a new compiler).

The major version number will be incremented to `1` once the entire SDK has
been reverse engineered (as we do not anticipate any API breaking changes).

## LICENSING
FST is released under the [`GNU General Public License (version 3 or later)`](https://www.gnu.org/licenses/gpl-3.0.en.html).

It is our understanding that if you use the FST-header in your project,
you have to use a compatible (Open Source) license for your project as well.

There are no plans to release FST under a more permissive license.
If you want to build closed-source plugins (or plugin hosts), you have to
use the official Steinberg VST2 SDK, and comply with their licensing terms.


## Miss a feature? File a bug!
So far, FST is not feature complete.
Opcodes that have not been reversed engineered yet are marked as `deprecated`,
so you get a compiler warning when using them.

If you find you need a specific opcode that is not supported yet, please file a
bug at https://git.iem.at/zmoelnig/FST/issues

The more information you can provide, the more likely we will be able to
implement missing or fix broken things.
