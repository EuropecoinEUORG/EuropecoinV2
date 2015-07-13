#!/bin/bash
# create multiresolution windows icon
ICON_DST=../../src/qt/res/icons/europecoin.ico

convert ../../src/qt/res/icons/europecoin-16.png ../../src/qt/res/icons/europecoin-32.png ../../src/qt/res/icons/europecoin-48.png ${ICON_DST}
