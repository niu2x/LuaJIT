LUAJIT=.
DEVDIR=`xcode-select -print-path`
IOSSDKVER=9.0
IOSDIR=$DEVDIR/Platforms
IOSBIN=$DEVDIR/Toolchains/XcodeDefault.xctoolchain/usr/bin



ISDKF="-arch arm64 -isysroot $IOSDIR/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk -miphoneos-version-min=8.0"
make HOST_CC="clang " CROSS=${IOSBIN}/ \
STATIC_CC=${IOSBIN}/clang DYNAMIC_CC="${IOSBIN}/clang -fPIC" \
TARGET_LD=${IOSBIN}/clang \
TARGET_CFLAGS="${ISDKF}" \
TARGET_SYS=iOS || true

lipo   -arch arm64 src/libluajit.a  -create -output libluajit.a
