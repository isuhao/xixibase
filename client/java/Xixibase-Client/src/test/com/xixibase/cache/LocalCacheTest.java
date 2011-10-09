/*
   Copyright [2011] [Yao Yuan(yeaya@163.com)]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

package com.xixibase.cache;

import junit.framework.TestCase;

public class LocalCacheTest extends TestCase {

	static String servers;
	static {
		servers = System.getProperty("hosts");
		if (servers == null) {
			servers = "localhost:7788";
		}
	}

	protected void setUp() throws Exception {
		super.setUp();
	}

	protected void tearDown() throws Exception {
		super.tearDown();
	}
	
	public void testLocalCache() throws InterruptedException {
		CacheClientManager mgr = CacheClientManager.getInstance("LocalCacheTest");
		String[] serverlist = servers.split(",");
		mgr.initialize(serverlist);
		mgr.enableLocalCache();
		Thread.sleep(50);
		LocalCache lc = mgr.getLocalCache();
		CacheClient cc = mgr.createClient();
		assertEquals(lc.getCacheCount(), 0);
		cc.setW("xixi", "value");
		assertEquals(lc.get("xixi").getValue(), "value");
		assertEquals(cc.get("xixi"), "value");
		assertEquals(cc.getL("xixi"), "value");
		assertEquals(cc.getW("xixi"), "value");
		assertEquals(cc.getLW("xixi"), "value");
		assertEquals(lc.get("xixi").getValue(), "value");
		assertEquals(lc.getCacheCount(), 1);
		assertEquals(lc.getCacheSize(), 1029);
		assertEquals(lc.getMaxCacheSize(), 64 * 1024 * 1024);
		lc.setMaxCacheSize(1024 * 1024);
		assertEquals(lc.getMaxCacheSize(), 1024 * 1024);
		lc.setWarningCacheRate(0.8);
		assertEquals(lc.getWarningCacheRate(), 0.8);
		
		mgr.disableLocalCache();
		assertEquals(cc.get("xixi"), "value");
		assertEquals(cc.getL("xixi"), "value");
		assertEquals(cc.getW("xixi"), "value");
		assertEquals(cc.getLW("xixi"), "value");
		

		mgr.shutdown();
	}
}
