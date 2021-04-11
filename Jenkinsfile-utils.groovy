#!/usr/bin/env groovy

// General structure of this file:
//
// SECTION: Utility functions.
// Random helper functions.
//
// SECTION: Stage functions.
// - stagePre()     : Function that should be invoked at the start of every stageFoo() function.
// - stagePost()    : Function that should be invoked at the end of every stageFoo() function.
// - stageFoo()     : A Jenkins stage.
//
// You should probably know about Groovy's elvis operator ?:,
// where a ?: b means
//      if (a) { return a; } else { return b; }

// SECTION: Utility functions.


// SECTION: Stage functions.

/** This should be invoked before every stage. */
void stagePre() {
    sh 'echo $NODE_NAME', label: 'Print node name.'
    sh script: './build-support/print_docker_info.sh', label: 'Print image information.'
}

/** This should be invoked after every stage. */
void stagePost() {
    // No-op.
}

/** Test if the GitHub "ready-for-ci" label is present. Otherwise, abort the build. */
void stageGithub() {
    stagePre()
    Integer ready_for_build = sh script: 'python3 ./build-support/check_github_labels.py', returnStatus: true, label: 'Test Github labels.'
    if (0 != ready_for_build) {
        currentBuild.result = 'ABORTED'
        error('Not ready for CI. Please add ready-for-ci tag in Github when you are ready to build your PR.')
    }
    stagePost()
}

return this
