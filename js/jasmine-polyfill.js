goog.require('goog.testing.TestCase');
goog.require('goog.testing.asserts');
goog.require('goog.testing.jsunit');

let test_case_;
/** @type{?function(): void} */
let pending = null;

function describe(caseName, callback) {
  test_case_ = new goog.testing.TestCase(caseName);
  callback();
  window['G_testRunner'].initialize(test_case_);
}

function it(testName, callback) {
  test_case_.add(new goog.testing.TestCase.Test(testName, callback));
}

/**
 * @param {*} actual
 * @constructor
 */
function Expect(actual) {
  this.actual = actual;
}

Expect.prototype.toEqual = function(expected) {
  assertEquals(expected, this.actual);
};

/**
 * @param {*} actual
 * @returns {!Expect}
 */
function expect(actual) {
  return new Expect(actual);
}
