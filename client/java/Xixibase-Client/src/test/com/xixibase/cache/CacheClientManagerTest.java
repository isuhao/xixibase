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

public class CacheClientManagerTest extends TestCase {
	static String[] serverlist;
	static String servers;
	static {
		servers = System.getProperty("hosts");
		if (servers == null) {
			servers = "localhost:7788";
		}
		serverlist = servers.split(",");
	}

	protected void setUp() throws Exception {
		super.setUp();
	//	CacheClientManager mgr1 = CacheClientManager.getInstance(managerName1);
	//	cc1 = mgr1.createClient();
	}

	protected void tearDown() throws Exception {
		super.tearDown();
	//	assertNotNull(cc1);
	//	cc1.flush();
	}

	public void testInitialize() {
		CacheClientManager mgr = CacheClientManager.getInstance();
		assertNotNull(mgr);
		assertEquals(mgr.getName(), "default");
		assertEquals(mgr.getDefaultPort(), 7788);
		assertEquals(mgr.getDefaultGroupID(), 0);
		mgr.setDefaultGroupID(315);
		mgr.setInitConn(2);
		assertEquals(mgr.getInitConn(), 2);
		mgr.setMaxActiveConn(7);
		assertEquals(mgr.getMaxActiveConn(), 7);
		mgr.setMaxBusyTime(10000);
		assertEquals(mgr.getMaxBusyTime(), 10000);
		mgr.setSocketConnectTimeout(15000);
		assertEquals(mgr.getSocketConnectTimeout(), 15000);
		mgr.setSocketTimeout(16000);
		assertEquals(mgr.getSocketTimeout(), 16000);
		mgr.setNagle(false);
		assertEquals(mgr.getNagle(), false);
		assertNull(mgr.getWeightMaper());
		mgr.setSocketWriteBufferSize(32 * 1024);
		assertEquals(mgr.getSocketWriteBufferSize(), 32 * 1024);
		assertEquals(mgr.getDefaultGroupID(), 315);
		assertFalse(mgr.isInitialized());
		boolean ret = mgr.initialize(serverlist);
		assertTrue(ret);
		assertTrue(mgr.isInitialized());
		assertNotNull(mgr.getWeightMaper());
		mgr.shutdown();
		assertFalse(mgr.isInitialized());
		assertNotNull(mgr.getServers());
	}
	
	public void testCreateSocket() {
		CacheClientManager mgr = CacheClientManager.getInstance();
		mgr.initialize(serverlist);
		XixiSocket socket = mgr.createSocket(serverlist[0]);
		assertNotNull(socket);
		
		socket = mgr.createSocket("unknownhost");
		assertNull(socket);
		mgr.shutdown();
	}
	
	public void testGetHost() {
		CacheClientManager mgr = CacheClientManager.getInstance();
		mgr.initialize(serverlist);
		String host = mgr.getHost("xixi");
		assertNotNull(host);
		mgr.shutdown();
	}
	
	public void testGetSocketByHost() {
		CacheClientManager mgr = CacheClientManager.getInstance();
		mgr.initialize(serverlist);
		XixiSocket socket = mgr.getSocketByHost(serverlist[0]);
		assertNotNull(socket);
		mgr.shutdown();
	}
	
	public void testError() {
		CacheClientManager mgr = CacheClientManager.getInstance();
		boolean ret = mgr.initialize(null);
		assertFalse(ret);
		ret = mgr.initialize(new String[0], null);
		assertFalse(ret);
		String[] s = new String[1];
		s[0] = "errorHost";
		ret = mgr.initialize(s);
		XixiSocket socket = mgr.getSocketByHost(serverlist[0]);
		assertNull(socket);
		socket = mgr.getSocketByHost("errorHost");
		assertNull(socket);
		mgr.shutdown();
	}
}