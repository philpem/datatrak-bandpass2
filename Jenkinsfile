pipeline {
    agent {
        docker {
            image 'bandpass2-build:latest'
            args  '-v /tmp/ccache:/ccache'
        }
    }

    environment {
        CCACHE_DIR = '/ccache'
    }

    stages {
        stage('Configure') {
            steps {
                sh 'cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release'
            }
        }

        stage('Build') {
            steps {
                sh 'cmake --build build --parallel'
            }
        }

        stage('Test') {
            steps {
                sh 'ctest --test-dir build --output-on-failure --output-junit test_results.xml'
            }
            post {
                always {
                    junit 'test_results.xml'
                }
            }
        }

        stage('Archive') {
            steps {
                archiveArtifacts artifacts: 'build/src/bandpass2', fingerprint: true
            }
        }
    }

    post {
        failure {
            emailext subject: "BANDPASS II build FAILED: ${env.BUILD_TAG}",
                     body: "See ${env.BUILD_URL}",
                     recipientProviders: [[$class: 'DevelopersRecipientProvider']]
        }
    }
}
