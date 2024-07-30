const path = require("node:path");
const fs = require("node:fs");
const vips = require("../../index.js");

console.log('js-lib-vips', vips);

console.log("result", vips.hello());
console.log("add", vips.add(12,13));

const sampleClass = new vips.ActualClass(4.3);
console.log('Testing class initial value: ', sampleClass.getValue());
console.log("After adding 3.3 ", sampleClass.add(3.3));

// Prepare output folder
const outputFolder = "../../output";
const outputFolderPath = path.resolve(__dirname, outputFolder);
if (!fs.existsSync(outputFolderPath)) {
  fs.mkdirSync(outputFolderPath);
}

const outputFileName = "countdown.gif";
const outputFilePath = path.resolve(outputFolderPath, outputFileName);

const countdownOptions = {
    width: 100,
    height: 100,
    bgColor: "#616161",
    outputFile: outputFilePath
}

console.log("Testing countdown: ", vips.countdown(countdownOptions));
