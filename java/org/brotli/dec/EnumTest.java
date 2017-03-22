/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.fail;

import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.TreeSet;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/**
 * Tests for Enum-like classes.
 */
@RunWith(JUnit4.class)
public class EnumTest {

  private void checkEnumClass(Class<?> clazz) {
    TreeSet<Integer> values = new TreeSet<Integer>();
    for (Field f : clazz.getDeclaredFields()) {
      assertEquals("int", f.getType().getName());
      assertEquals(Modifier.FINAL | Modifier.STATIC, f.getModifiers());
      Integer value = null;
      try {
        value = f.getInt(null);
      } catch (IllegalAccessException ex) {
        fail("Inaccessible field");
      }
      assertFalse(values.contains(value));
      values.add(value);
    }
    assertEquals(0, values.first().intValue());
    assertEquals(values.size(), values.last() + 1);
  }

  @Test
  public void testRunningState() {
    checkEnumClass(RunningState.class);
  }

  @Test
  public void testWordTransformType() {
    checkEnumClass(WordTransformType.class);
  }
}
