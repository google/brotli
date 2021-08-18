package org.brotli.wrapper.android;

import static junit.framework.Assert.assertEquals;

import androidx.test.core.app.ApplicationProvider;
import com.google.thirdparty.robolectric.GoogleRobolectricTestRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(GoogleRobolectricTestRunner.class)
public final class UseJniTest {

  @Before
  public void setup() {
    JniHelper.context = ApplicationProvider.getApplicationContext();
  }

  @Test
  public void testAnswerToTheUltimateQuestionOfLifeTheUniverseAndEverything() {
    assertEquals(42, UseJni.deepThought());
  }
}
