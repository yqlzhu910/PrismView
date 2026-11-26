# Third-party components

The Windows release bundles the unmodified runtime libraries and plugins from the OpenSceneGraph 3.4.0 / VC110 x64 SDK used to build PrismView (formerly STLQuickView).

## OpenSceneGraph and OpenThreads

- OpenSceneGraph 3.4.0, library ABI 130.
- OpenThreads 3.3.0, library ABI 20 (included with that OpenSceneGraph SDK).
- License: OpenSceneGraph Public License, version 0.0 or later, including the accompanying wxWindows exception and GNU Library General Public License text.
- Full license: [licenses/OpenSceneGraph-LICENSE.txt](licenses/OpenSceneGraph-LICENSE.txt).
- Upstream source release: https://github.com/openscenegraph/OpenSceneGraph/tree/OpenSceneGraph-3.4.0
- Source archive: https://github.com/openscenegraph/OpenSceneGraph/archive/refs/tags/OpenSceneGraph-3.4.0.zip

The runtime includes the OSG core, viewer, database, effects, interaction, utilities, text, and STL / OBJ / PLY / image plugins. These libraries are dynamically linked and have not been modified for this viewer.

## PLY plugin components

The OSG PLY plugin also contains code with the following notices. Its original notices are preserved in [licenses/PLY-NOTICES.txt](licenses/PLY-NOTICES.txt).

- PLY reader: Copyright (C) 2009 VizExperts Limited, LGPL 2.1 or later.
- Vertex data and type definitions: Copyright (c) 2007 Tobias Wolf; derived from Equalizer LGPL source.
- PLY routines: Copyright (c) 1994 The Board of Trustees of The Leland Stanford Junior University; Copyright (c) 2005-2007 Stefan Eilemann for subsequent changes. The Stanford permission includes a condition that the software is not sold; retain its complete notice when redistributing this free package.
- Full LGPL 2.1 text: [licenses/LGPL-2.1.txt](licenses/LGPL-2.1.txt), obtained from the [SPDX license list](https://github.com/spdx/license-list-data/blob/main/text/LGPL-2.1-or-later.txt).
- Corresponding PLY source: https://github.com/openscenegraph/OpenSceneGraph/tree/OpenSceneGraph-3.4.0/src/osgPlugins/ply (also included in the full source archive linked above).

## libpng

- The bundled SDK identifies libpng 1.6.2.
- Copyright and license notice, preserved from that SDK's `png.h`: [licenses/libpng-LICENSE.txt](licenses/libpng-LICENSE.txt).
- Upstream project: https://libpng.org/pub/png/libpng.html

## zlib

- zlib 1.2.8.
- Copyright (C) 1995-2013 Jean-loup Gailly and Mark Adler.
- Notice preserved from the bundled SDK's `zlib.h`: [licenses/zlib-LICENSE.txt](licenses/zlib-LICENSE.txt).
- Upstream project: https://zlib.net/

## Independent JPEG Group

This software is based in part on the work of the Independent JPEG Group.

The SDK's JPEG headers identify compatibility version 9.0. The JPEG reader is provided by the OSG JPEG plugin. The original JPEG 9 README, including its legal notices, is provided in [licenses/IJG-README.txt](licenses/IJG-README.txt).

Upstream source distribution: https://ijg.org/files/jpegsrc.v9.tar.gz

## Microsoft Visual C++ 2012 Runtime

The Windows package includes `msvcp110.dll` and `msvcr110.dll` from the Visual C++ 2012 x64 redistributable directory. These files are Microsoft software and are governed by Microsoft's license terms, not the open-source licenses above.

Official redistributable information: https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist

Keep these notices and the `licenses` directory with the distributed program.
