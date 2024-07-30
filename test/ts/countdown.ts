import fs from 'node:fs';
import path from 'node:path';
import vips, { CountdownOptions } from '../../index';

// Prepare output folder
const outputFolder = "../../output";
const outputFolderPath = path.resolve(__dirname, outputFolder);
if (!fs.existsSync(outputFolderPath)) {
  fs.mkdirSync(outputFolderPath);
}

const outputFileName = "countdown.gif";
const outputFilePath: string = path.resolve(outputFolderPath, outputFileName);

const countdownOptions: CountdownOptions = {
    width: 100,
    height: 100,
    bgColor: "#616161",
    outFilePath: outputFilePath
}

console.log("Testing countdown: ", vips.countdown(countdownOptions));