#*********************************************************************************
#
# Inviwo - Interactive Visualization Workshop
#
# Copyright (c) 2013-2015 Inviwo Foundation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# 
#*********************************************************************************
import os
import itertools
import glob
import datetime
import json

from pdb import set_trace as bp

from . import inviwoapp
from . import test
from .. util import *
from . imagecompare import *
from . generatereport import *


def findModuleTest(path):
	# assume path points to a folder of modules.
	# look in folder path/<module>/tests/regression/*
	tests = []
	for moduleDir in subDirs(path):
		regressionDir = toPath([path, moduleDir, "tests", "regression"])
		for testDir in subDirs(regressionDir):
			tests.append(test.Test(
				kind = "module", 
				name = testDir,
				module = moduleDir,
				path = toPath([regressionDir, testDir]) 
				))
	return tests

def findRepoTest(path):
	# assume path points to a repo.
	# look for tests in path/tests/regression
	tests = []
	regressionDir = toPath([path, "tests", "regression"])
	for testDir in subDirs(regressionDir):
		tests.append(test.Test(
					kind = "repo", 
					name = testDir,
					repo = path.split("/")[-1],
					path = toPath([regressionDir, testDir]) 
					))
	return tests

class ReportTest:
	def __init__(self, key, testfun, message):
		self.key = key
		self.testfun = testfun
		self.message = message

	def test(self, report):
		return self.testfun(report[self.key])

	def failures(self):
		return [self.message]

class ReportImageTest(ReportTest):
	def __init__(self, key):
		self.key = key
		self.message = []

	def test(self, report):
		imgs = report[self.key]
		for img in imgs:
			if img["difference"] != 0.0:
				self.message.append(
					"Image {image} has non-zero ({difference}%) difference".format(**img))

		return len(self.message) == 0

	def failures(self):
		return self.message


class App:
	def __init__(self, appPath, outputPath, moduleTestPaths = [], 
				 repoTestPaths = [], settings = inviwoapp.RunSettings()):
		self.app = inviwoapp.InviwoApp(appPath, settings)
		self.output = outputPath
		tests = ([findModuleTest(p) for p in moduleTestPaths] 
				 + [findRepoTest(p) for p in repoTestPaths])
		self.tests = list(itertools.chain(*tests))

	def runTest(self, test):
		print_info("#"*80)
		print_pair("Regression", test.toString())

		report = {}
		report['date'] = datetime.datetime.now().isoformat()
		report = test.report(report)

		report = self.app.runTest(test, report, self.output)
		report = self.compareImages(test, report)

		report = self.checkReport(report)

		for k,v in report.items():
			print_pair(k,str(v))
		print()
		return report

	def runTests(self, testrange = slice(0,None), testfilter = lambda x: True):
		tests = self.tests[testrange]
		tests = list(filter(testfilter, tests))

		print_info("Running {} tests".format(len(tests)))
		self.reports = []
		for test in tests:
			report = self.runTest(test)
			self.reports.append(report)

	def compareImages(self, test, report):
		refimgs = test.getImages()
		refs = set(refimgs)

		outputdir = test.makeOutputDir(self.output)
		imgs = glob.glob(outputdir +"/*.png")
		imgs = [os.path.relpath(x, outputdir) for x in imgs]
		imgs = set(imgs) - set(["screenshot.png"])

		report["refs"] = list(refimgs)
		report["imgs"] = list(imgs)
		report['missing_refs'] = list(imgs - refs)
		report['missing_imgs'] = list(refs - imgs)

		imgtests = []
		for img in imgs:
			if img in refs:
				comp = ImageCompare(toPath([outputdir, img]), toPath([test.path, img]))
				diff = comp.difference()
				imgtest = {
					'image' : img,
					'difference' : diff
				}
				imgtests.append(imgtest)

		report['image_tests'] = imgtests

		return report


	def checkReport(self, report):
		tests = [
			ReportTest('returncode', lambda x : x == 0, "Non zero retuncode"),
			ReportTest('timeout', lambda x : x == False, "Inviwo ran out of time"),
			ReportTest('missing_refs', lambda x : len(x) == 0, "Missing refecence image"),
			ReportTest('missing_imgs', lambda x : len(x) == 0, "Missing test image"),
			ReportImageTest('image_tests')
		]
		failures = []
		for t in tests:
			if not t.test(report):
				failures += t.failures()

		report['failures'] = failures

		return report


	def saveJson(self, file):
		with open(file, 'w') as f:
			json.dump(self.reports, f, indent=4, separators=(',', ': '))

	def saveHtml(self, file):
	    with open(file, 'w') as f:
	    	html = HtmlReport(self.reports)
    		f.write(html.getHtml())