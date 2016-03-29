#!/bin/sh

#  link_bundle.sh
#  yammer
#
#  Created by david on 3/29/16.
#  Copyright © 2016 combobulated. All rights reserved.

MY_LINK="${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}/Yammer.framework"
if [[ ! -e "$MY_LINK" ]] && [[ "$CONFIGURATION" == "Debug" ]]; then
    mkdir -p "${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}"
    ln -s "${TARGET_BUILD_DIR}/Yammer.framework" "$MY_LINK"
fi
