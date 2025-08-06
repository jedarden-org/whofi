module.exports = {
  testEnvironment: 'node',
  coverageDirectory: 'coverage',
  testMatch: [
    '**/*.test.js'
  ],
  collectCoverageFrom: [
    '**/*.js',
    '!**/node_modules/**',
    '!**/coverage/**',
    '!jest.config.js'
  ],
  testTimeout: 10000
};