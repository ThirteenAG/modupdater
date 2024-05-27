###############################################################################
### This script is called by (make download-external-libs). It will git clone
### thirdparts needed for this project but does not compile them. It replaces
### git submodules that I dislike.
###############################################################################

function fatal
{
    echo -e "\033[35mFailure! $1\033[00m"
    exit 1
}

### $1 is given by ../Makefile and refers to the current architecture.
ARCHI="$1"
if [ "$ARCHI" == "" ]; then
    fatal "Define the architecture as $1 (i.e Linux, Darwin or Windows)"
fi

### $2 is given by ../Makefile and refers to the current target.
TARGET="$2"
if [ "$TARGET" == "" ]; then
    fatal "Define the binary target as $2"
fi

###
readonly GITHUB_URL="https://github.com"
readonly SAVANA_URL="http://git.savannah.gnu.org/git"

### Git clone a GitHub repository $1
URL=$GITHUB_URL
function cloning
{
    REPO=$1

    echo -e "\033[35m*** Cloning: \033[36m$URL/$REPO\033[00m => \033[33m$TARGET\033[00m"
    if [ "$URL" == "$GITHUB_URL" ]; then
        ARR=(${REPO//\// })
        if [ "${ARR[1]}" == "" ]; then
            fatal "Malform repository. Shall be user_name/repo_name"
        else
            rm -fr ${ARR[1]}
        fi
    else
        rm -fr $REPO
    fi

    shift
    git clone $URL/$REPO --depth=1 --recurse $* > /dev/null

    # Restore
    URL=$GITHUB_URL
}

function cloning_non_recurse
{
    REPO=$1

    echo -e "\033[35m*** Cloning: \033[36m$URL/$REPO\033[00m => \033[33m$TARGET\033[00m"
    if [ "$URL" == "$GITHUB_URL" ]; then
        ARR=(${REPO//\// })
        if [ "${ARR[1]}" == "" ]; then
            fatal "Malform repository. Shall be user_name/repo_name"
        else
            rm -fr ${ARR[1]}
        fi
    else
        rm -fr $REPO
    fi

    shift
    git clone $URL/$REPO --depth=1 $* > /dev/null

    # Restore
    URL=$GITHUB_URL
}