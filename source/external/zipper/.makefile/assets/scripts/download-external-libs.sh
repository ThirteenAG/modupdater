#!/bin/bash -e
###############################################################################
### This script is called by (make download-external-libs). It will git clone
### thirdparts needed for this project but does not compile them. It replaces
### git submodules that I dislike.
###############################################################################
###
### Input of this script:
### $1: target (your application name)
### $2: script to include owning what to git clone.
### $3: GitHub_user_name/GitHub_repo_name
###############################################################################

function fatal
{
    echo -e "\033[35mFailure! $1\033[00m"
    exit 1
}

readonly GITHUB_URL="https://github.com"

PROJECT_NAME=$1
function do_cloning_
{
    URL=$1
    REPO=$2
    shift
    shift

    echo -e "\033[35m*** Cloning: \033[36m$URL/$REPO\033[00m => \033[33m$PROJECT_NAME\033[00m"
    if [ "$URL" == "$GITHUB_URL" ]; then
        ARR=(${REPO//\// })
        if [ "${ARR[1]}" == "" ]; then
            fatal "Malform repository. Shall be user_name/repo_name"
        else
            rm -fr ${ARR[1]}
        fi
    else
        fatal "Only GitHub repo is managed for now"
    fi

    git clone $URL/$REPO --depth=1 $* > /dev/null
}

function cloning_non_recurse
{
    REPO=$1
    shift
    do_cloning_ $GITHUB_URL $REPO $*
}

function cloning
{
    REPO=$1
    shift
    do_cloning_ $GITHUB_URL $REPO --recurse $*
}

if [ ! -z "$2" ]; then
    source $2
fi
